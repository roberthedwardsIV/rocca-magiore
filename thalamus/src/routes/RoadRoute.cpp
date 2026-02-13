#include "RoadRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

// Constructor for initialization
RoadRoute::RoadRoute(long long id, std::string name)
    : BaseRoute(id, name, "road") {
    
    // Default values
    base_capacity_vph = 1000.0f; 
    current_flow_vph = 0.0f;
    max_weight_tons = 40.0f;     
    surface_quality = 1.0f;      
    base_friction = 0.7f;        
    current_friction = 0.7f;
    heavy_vehicle_pct = 0.15f;   
    lanes = 1;
    max_speed_kmh = 50;
    is_one_way = false;
    classification = "road";
    effective_speed_kmh = 50.0f;
    travel_time_hours = 0.0f;
    current_weight_limit = max_weight_tons;
    logistics_accessible = true;
}


// Direct updates to static fields from OSM tags
void RoadRoute::parse_osm_tags() {
    if (tags.count("highway")) classification = tags["highway"];

    if (tags.count("lanes")) {
        try { lanes = std::stoi(tags["lanes"]); } catch (...) { lanes = 1; }
    } else {
        if (classification == "motorway" || classification == "trunk") lanes = 2;
        else lanes = 1;
    }

    // Maxspeed converted to kmh from mph if needed
    if (tags.count("maxspeed")) {
        std::string speed_str = tags["maxspeed"];
        if (speed_str.find("mph") != std::string::npos) {
            try { max_speed_kmh = (int)(std::stoi(speed_str.substr(0, speed_str.find(" "))) * 1.60934); } 
            catch (...) { max_speed_kmh = 50; }
        } else if (speed_str == "none") {
            max_speed_kmh = 130; 
        } else {
            try { max_speed_kmh = std::stoi(speed_str); } catch (...) { max_speed_kmh = 50; }
        }
    } else {
        if (classification == "motorway") max_speed_kmh = 110;
        else if (classification == "trunk") max_speed_kmh = 90;
        else if (classification == "primary") max_speed_kmh = 80;
        else if (classification == "residential") max_speed_kmh = 30;
        else max_speed_kmh = 50;
    }

    if (tags.count("oneway")) is_one_way = (tags["oneway"] == "yes");

    // Max Weight based on road engineering class
    if (classification == "motorway" || classification == "trunk") max_weight_tons = 60.0f;
    else if (classification == "residential") max_weight_tons = 15.0f;
    else max_weight_tons = 40.0f;
    current_weight_limit = max_weight_tons;

    // Capacity Calculation
    float lane_capacity = (classification == "motorway" || classification == "trunk") ? 2200.0f : 1200.0f;
    float usable_lanes = is_one_way ? (float)lanes : (float)lanes / 2.0f;
    if (usable_lanes < 1.0f) usable_lanes = 1.0f; 
    base_capacity_vph = usable_lanes * lane_capacity * surface_quality;
    
    update_metrics();
}


// Updates to dynamic metrics (Kinematics, Load Limits, and BPR)
void RoadRoute::update_metrics() {

    // Safe Speed via Braking Kinematics
    // Safe stopping distance (d = v^2 / 2*mu*g), 
    // Safe velocity scales by the square root of the friction ratio.
    float friction_ratio = current_friction / base_friction;
    effective_speed_kmh = max_speed_kmh * std::sqrt(friction_ratio);

    // Passing Friction Penalty (Non-motorway, two-way roads)
    if (!is_one_way && lanes <= 2) {
        effective_speed_kmh *= 0.85f;
    }
    if (effective_speed_kmh < 5.0f) effective_speed_kmh = 5.0f;

    // Structural Accessibility
    if (current_weight_limit < 40.0f) {
        logistics_accessible = false;
    } else {
        logistics_accessible = true;
    }

    // Flow Conversion (Passenger Car Equivalent - PCE)
    // E_T (Equivalent Factor) is 2.0 on flat motorways, 4.0 on tight residential roads.
    float equivalent_factor = (classification == "motorway" || classification == "trunk") ? 2.0f : 4.0f;
    
    // PCE_Flow = VPH * (1 + %Trucks * (E_T - 1))
    float pce_flow = current_flow_vph * (1.0f + heavy_vehicle_pct * (equivalent_factor - 1.0f));

    // Latency (BPR Function)
    if (get_length_km() > 0) {
        float t_free = get_length_km() / effective_speed_kmh;
        // Feed the heavy-vehicle adjusted PCE flow into the BPR calculation
        travel_time_hours = calculate_bpr_delay(pce_flow, base_capacity_vph) * t_free;
    }
}


// Helper BPR Function for latency calculations
float RoadRoute::calculate_bpr_delay(float volume, float capacity) const {
    if (capacity <= 0.0f) return 9999.0f; // Blocked
    const float alpha = 0.15f;
    const float beta = 4.0f;
    float saturation = volume / capacity;
    return 1.0f + (alpha * std::pow(saturation, beta));
}


// Main signal routing function to apply dynamic updates
void RoadRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::string category = sig.value("category", "none");

    // Traffic + Congestion (current_flow_vph)
    if (category == "traffic" || category == "flow") {
        if (sig.contains("current_vph")) {
            current_flow_vph = sig["current_vph"].get<float>();
        }
        else if (sig.contains("congestion_level")) {
            float saturation = sig["congestion_level"].get<float>();
            current_flow_vph = base_capacity_vph * saturation;
        }
    }

    // Weather (current_friction)
    else if (category == "weather") {
        std::string condition = sig.value("condition", "");
        if (condition == "rain") current_friction = 0.45f;       // Wet asphalt
        else if (condition == "snow") current_friction = 0.20f;  // Packed snow
        else if (condition == "ice") current_friction = 0.10f;   // Black ice
        else if (condition == "clear") current_friction = base_friction; // 0.7
    }

    // Integrity (surface_quality + current_weight_limit)
    else if (category == "integrity") {
        float dmg = sig.value("severity", 0.0f); // 0.0 = none, 1.0 = destroyed
        surface_quality = std::max(0.0f, 1.0f - dmg);
        
        // Structural damage directly reduces the safe bearing weight of the road/subgrade
        current_weight_limit = max_weight_tons * surface_quality;
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}


// JSON packager for archival
json RoadRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(route_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"flow_vph", current_flow_vph},
        {"effective_speed", effective_speed_kmh},
        {"travel_time_h", travel_time_hours},
        {"current_friction", current_friction},
        {"weight_limit_t", current_weight_limit},
        {"logistics_accessible", logistics_accessible},
        {"last_update", last_update}
    };
}