#include "RailNodeHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- RAIL YARD PHYSICS CONSTANTS ---
    
    // Geometry
    constexpr float WAGON_LEN_FREIGHT = 18.0f;    // Standard UIC/AAR Wagon
    constexpr float WAGON_LEN_CONTAINER = 14.0f;  // Intermodal Flatcar (40ft + gaps)
    constexpr float TRACK_LEN_DEFAULT = 600.0f;   // Fallback siding length
    constexpr float HUMP_TRACK_THRESHOLD = 20.0f; // >20 tracks usually implies a hump

    // Hump Yard Kinematics (Continuous Flow)
    constexpr float SPEED_HUMP_NORMAL = 3.5f;     // km/h (Walking pace)
    constexpr float SPEED_HUMP_FAST = 5.0f;       // km/h (High throughput)

    // Flat Switching Kinematics (Batch Process)
    constexpr float ACCEL_SWITCHER = 0.3f;        // m/s^2 (Diesel-Electric, loaded)
    constexpr float TIME_UNCOUPLE = 30.0f;        // Seconds to bleed air/pull pin per cut
    constexpr float DIST_SHUNT_DEFAULT = 400.0f;  // Avg distance per move

    // Queuing Theory (Kingman's Formula Parameters)
    constexpr float VARIATION_COEFF = 1.0f;       // Exponential arrival distribution (CoV=1)
    constexpr float MIN_DWELL_HOURS = 12.0f;      // Inspection/Crew Change/Paperwork baseline
    constexpr float MAX_UTILIZATION_CLAMP = 0.99f;// Prevent divide-by-zero at 100% capacity
}

RailNodeHub::RailNodeHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "rail_node", lat, lon) {
    
    // Zero-Init (Parsed later)
    number_of_tracks = 0;
    max_train_length_m = 0.0f; 
    is_hump_yard = false;
    is_electrified = false;
    has_intermodal_lift = false;

    // Default Physics (Standard Freight)
    hump_speed_kmh = SPEED_HUMP_NORMAL;
    switcher_accel_ms2 = ACCEL_SWITCHER;
    avg_wagon_length_m = WAGON_LEN_FREIGHT;

    // State
    max_storage_wagons = 0;
    current_wagons_stored = 0;
    
    // Metrics
    service_rate_wagons_hr = 0.0f;
    arrival_rate_wagons_hr = 0.0f;
    utilization_rho = 0.0f;
    average_dwell_time_h = MIN_DWELL_HOURS;
    
    yard_type = "siding";
}

void RailNodeHub::parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) {
    // 1. Classification
    if (tags.count("railway")) {
        std::string r = tags.at("railway");
        if (r == "yard" || r == "marshalling_yard") {
            yard_type = "marshalling";
            number_of_tracks = 10; // Baseline
            max_train_length_m = 1000.0f;
        } else if (r == "intermodal_terminal") {
            yard_type = "intermodal";
            has_intermodal_lift = true;
            avg_wagon_length_m = WAGON_LEN_CONTAINER;
        }
    }

    // 2. Track Count & Hump Inference
    if (tags.count("tracks")) {
        try { number_of_tracks = std::stoi(tags.at("tracks")); } catch (...) {}
    }

    if (tags.count("railway:yard:type") && tags.at("railway:yard:type") == "hump") {
        is_hump_yard = true;
    } else if (number_of_tracks >= HUMP_TRACK_THRESHOLD) {
        is_hump_yard = true; 
    }

    // 3. Geometric Capacity
    if (tags.count("length")) {
        try { max_train_length_m = std::stof(tags.at("length")); } catch (...) {}
    }
    
    float effective_track_len = (max_train_length_m > 0) ? max_train_length_m : TRACK_LEN_DEFAULT;
    // Total Capacity = Tracks * Length / WagonSize
    // Note: Yards are rarely 100% efficient packing; assume 90% usable length
    max_storage_wagons = (int)((float)number_of_tracks * effective_track_len * 0.9f / avg_wagon_length_m);

    // 4. Electrification
    if (tags.count("electrified")) {
        std::string e = tags.at("electrified");
        if (e == "yes" || e == "contact_line") is_electrified = true;
    }
    
    update_metrics();
}

void RailNodeHub::update_metrics() {
    // --- 1. Physics-Based Service Rate (mu) ---
    if (is_hump_yard) {
        // CONTINUOUS FLOW:
        // Rate = Speed / Wagon Length
        float speed_ms = hump_speed_kmh / 3.6f;
        float wagons_per_sec = speed_ms / avg_wagon_length_m;
        service_rate_wagons_hr = wagons_per_sec * 3600.0f;
    } else {
        // BATCH FLOW (Flat Switching):
        // Cycle = Accel + Decel + Work
        // d = shunt distance (approx half train length)
        float d = max_train_length_m * 0.5f; 
        
        // Kinematics: t = sqrt(2d/a) assuming start from stop
        float t_move = std::sqrt(2.0f * d / switcher_accel_ms2); 
        
        // Total Cycle = Move Out + Uncouple + Move Back
        float cycle_seconds = (t_move * 2.0f) + TIME_UNCOUPLE;
        
        // Throughput = (3600 / CycleTime) * WagonsPerCut
        // Assume avg 1.5 wagons per cut in mixed freight
        service_rate_wagons_hr = (3600.0f / cycle_seconds) * 1.5f;
    }

    // --- 2. Utilization (rho) ---
    // Infer arrival rate from fill percentage if no live signal
    float fill_pct = 0.0f;
    if (max_storage_wagons > 0) {
        fill_pct = (float)current_wagons_stored / (float)max_storage_wagons;
    }
    
    // Assume Arrivals scale with Inventory Pressure (Feedback loop)
    // If we don't have explicit arrival data, assume rho ~= fill_pct
    utilization_rho = std::min(MAX_UTILIZATION_CLAMP, std::max(0.01f, fill_pct)); 

    // --- 3. Dwell Time (Kingman's Formula) ---
    // E(W) = (rho / (1 - rho)) * (Variation / 2) * t_service
    float t_service_h = 1.0f / std::max(1.0f, service_rate_wagons_hr);
    
    // Queue time explodes as rho -> 1
    float queue_time_h = (utilization_rho / (1.0f - utilization_rho)) * t_service_h;
    
    average_dwell_time_h = MIN_DWELL_HOURS + queue_time_h;
}

void RailNodeHub::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(hub_mutex);
    std::string category = sig.value("category", "none");

    // Inventory / Logistics Signals
    if (category == "logistics" || category == "inventory") {
        if (sig.contains("wagon_count")) {
            current_wagons_stored = sig["wagon_count"].get<int>();
        } else if (sig.contains("fill_pct")) {
            current_wagons_stored = (int)(max_storage_wagons * sig["fill_pct"].get<float>());
        }
    }
    
    // Physical Degradation (Switcher health / Hump maintenance)
    else if (category == "mechanical") {
        float health = 1.0f - sig.value("severity", 0.0f);
        
        // Physics update: Degraded locos accelerate slower
        switcher_accel_ms2 = ACCEL_SWITCHER * health; 
        
        // Physics update: Degraded humps run slower
        hump_speed_kmh = SPEED_HUMP_NORMAL * health;
    }
    
    // Weather (Friction)
    else if (category == "weather") {
        // Rain/Snow reduces traction -> lowers acceleration
        std::string cond = sig.value("condition", "");
        if (cond == "snow" || cond == "ice") {
            switcher_accel_ms2 *= 0.7f; // Loss of traction
            hump_speed_kmh *= 0.8f;     // Safety reduction
        }
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

bool RailNodeHub::can_accept_train(float train_length_m) const {
    if (train_length_m > max_train_length_m) return false;
    // Congestion Gate: If delays exceed 4 days (96h), refuse entry
    if (average_dwell_time_h > 96.0f) return false;
    return true;
}

json RailNodeHub::get_json_state() const {
    std::lock_guard<std::mutex> lock(hub_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"yard_type", yard_type},
        {"is_hump", is_hump_yard},
        {"capacity_wagons", max_storage_wagons},
        {"inventory", current_wagons_stored},
        {"utilization", utilization_rho},
        {"service_rate_wph", service_rate_wagons_hr},
        {"avg_dwell_time_h", average_dwell_time_h},
        {"physics", {
            {"hump_speed", hump_speed_kmh},
            {"accel", switcher_accel_ms2}
        }},
        {"last_update", last_update}
    };
}