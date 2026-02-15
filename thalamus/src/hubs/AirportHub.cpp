#include "AirportHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- AVIATION PHYSICS CONSTANTS ---
    
    // Aircraft Footprints (Parking) including clearance
    constexpr float AREA_747_FREIGHTER = 5600.0f;  // ~80m x 70m
    constexpr float AREA_737_FREIGHTER = 1600.0f;  // ~40m x 40m
    
    // Runway Physics (Heavy Aircraft Focus)
    constexpr float ROT_HEAVY_SECONDS = 50.0f;     // Runway Occupancy Time (Braking)
    constexpr float WAKE_SEP_HEAVY_NM = 4.0f;      // Nautical Miles spacing behind Super/Heavy
    constexpr float APPROACH_SPEED_HEAVY = 140.0f; // Knots
    
    // Logistics
    constexpr float DENSITY_COLD_STORAGE = 0.4f;   // Tons per m3 (Pharma/Perishables)
}

AirportHub::AirportHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "airport", lat, lon) {
    
    // Default Infrastructure
    number_of_runways = 1;
    max_runway_length_m = 1500.0f; 
    cargo_apron_area_sqm = 5000.0f; // Small ramp
    has_rail_access = false;

    // Physics Defaults (Regional)
    avg_runway_occupancy_time_s = ROT_HEAVY_SECONDS;
    wake_turbulence_separation_nm = WAKE_SEP_HEAVY_NM; 
    approach_speed_knots = APPROACH_SPEED_HEAVY;

    // State
    max_parking_spots = 1;
    current_parked_freighters = 0;
    cold_storage_volume_m3 = 0.0f;
    current_cold_storage_used = 0.0f;

    // Operations
    max_slots_per_hour = 0.0f;
    current_slot_utilization = 0.0f;
    is_curfew_active = false;

    iata_code = "UNK";
    icao_code = "ZZZZ";
    airport_type = "regional";
}

void AirportHub::parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) {
    // 1. Identifiers
    if (tags.count("iata")) iata_code = tags.at("iata");
    if (tags.count("icao")) icao_code = tags.at("icao");

    // 2. Type & Infrastructure Heuristics
    if (tags.count("aerodrome:type")) airport_type = tags.at("aerodrome:type");
    else if (tags.count("aeroway") && tags.at("aeroway") == "aerodrome") airport_type = "public";

    if (airport_type == "international" || airport_type == "major") {
        number_of_runways = 2;
        max_runway_length_m = 3500.0f; // Heavy capable
        cargo_apron_area_sqm = 100000.0f; // Massive ramp
        has_rail_access = true;
        cold_storage_volume_m3 = 5000.0f;
    } 
    else if (airport_type == "military") {
        max_runway_length_m = 3000.0f;
        cargo_apron_area_sqm = 50000.0f;
        is_curfew_active = false;
    }
    else {
        // Regional/General
        number_of_runways = 1;
        max_runway_length_m = 2000.0f;
        cargo_apron_area_sqm = 10000.0f;
    }

    // 3. Explicit Overrides
    if (tags.count("runway:length")) {
        try { max_runway_length_m = std::stof(tags.at("runway:length")); } catch (...) {}
    }
    
    // Cargo Apron Area (if mapped polygon)
    if (tags.count("area") && tags.count("aeroway") && tags.at("aeroway") == "apron") {
        try { cargo_apron_area_sqm = std::stof(tags.at("area")); } catch (...) {}
    }

    // 4. Calculate Parking Spots (Physics)
    // How many 747s fit? Or 737s?
    // If runway > 3000m, assume Heavy Freighters (747). Else Feeder Freighters (737).
    float footprint = (max_runway_length_m >= 3000.0f) ? AREA_747_FREIGHTER : AREA_737_FREIGHTER;
    
    // Packing efficiency ~60% (taxi lanes, GSE storage)
    max_parking_spots = std::max(1, (int)((cargo_apron_area_sqm * 0.6f) / footprint));
    
    update_metrics();
}

void AirportHub::update_metrics() {
    // --- 1. Runway Throughput Physics ---
    // Capacity is limited by Wake Turbulence Separation or ROT, whichever is greater.
    
    // Time separation due to wake (Hours)
    // T_wake = Dist / Speed
    float time_separation_h = wake_turbulence_separation_nm / approach_speed_knots;
    float time_separation_s = time_separation_h * 3600.0f;
    
    // Critical Time Interval (The bottleneck)
    // Aircraft cannot land closer than ROT or Wake Separation allows.
    float critical_interval_s = std::max(avg_runway_occupancy_time_s, time_separation_s);
    
    // Slots per hour per runway
    float capacity_per_runway = 3600.0f / critical_interval_s;
    
    // Total Capacity (Mixed Mode factor 1.0 for independent, <2.0 for dependent)
    // Assume 80% efficiency for multi-runway coordination
    float multi_runway_factor = (number_of_runways > 1) ? 1.8f : 1.0f;
    
    max_slots_per_hour = capacity_per_runway * multi_runway_factor;

    // --- 2. Curfew Logic ---
    if (is_curfew_active) {
        max_slots_per_hour = 0.0f;
    }
}

void AirportHub::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(hub_mutex);
    std::string category = sig.value("category", "none");

    // Traffic Flow (ATC)
    if (category == "traffic" || category == "atc") {
        if (sig.contains("utilization")) {
            current_slot_utilization = sig["utilization"].get<float>();
        }
        if (sig.contains("parked_count")) {
            current_parked_freighters = sig["parked_count"].get<int>();
        }
    }
    
    // Weather (Visiblity reduces capacity)
    else if (category == "weather") {
        if (sig.contains("visibility_m")) {
            float vis = sig["visibility_m"].get<float>();
            // Low Visibility Procedures (LVP) increase separation massively
            if (vis < 550.0f) { // CAT I Minima
                wake_turbulence_separation_nm = 10.0f; // Approx 2.5x spacing
            } else {
                wake_turbulence_separation_nm = WAKE_SEP_HEAVY_NM; // Reset
            }
        }
    }

    // Time/Curfew
    else if (category == "time") {
        if (sig.contains("is_night")) {
            bool night = sig["is_night"].get<bool>();
            // Military/24h airports ignore curfew
            if (night && airport_type != "military" && airport_type != "major_hub_24h") {
                is_curfew_active = true;
            } else {
                is_curfew_active = false;
            }
        }
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

bool AirportHub::can_accept_aircraft(const std::string& type_class, float required_runway_m) const {
    // 1. Runway Physics
    if (required_runway_m > max_runway_length_m) return false;
    
    // 2. Parking Physics
    if (current_parked_freighters >= max_parking_spots) return false;
    
    // 3. Operational State
    if (is_curfew_active) return false;
    
    return true;
}

json AirportHub::get_json_state() const {
    std::lock_guard<std::mutex> lock(hub_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"iata", iata_code},
        {"runways", number_of_runways},
        {"max_runway_m", max_runway_length_m},
        {"apron_sqm", cargo_apron_area_sqm},
        {"parking_spots", max_parking_spots},
        {"parked", current_parked_freighters},
        {"slots_ph", max_slots_per_hour},
        {"utilization", current_slot_utilization},
        {"curfew", is_curfew_active},
        {"last_update", last_update}
    };
}