#include "RoadRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

RoadRoute::RoadRoute(long long id, std::string name)
    : BaseRoute(id, name, "road") {
    
    // Default quantitative baselines
    base_capacity_vph = 1000.0f; // Single lane default
    current_flow_vph = 0.0f;
    max_weight_tons = 40.0f;     // Standard truck limit
    surface_quality = 1.0f;      // Assume good condition initially
    length_km = 0.0f;            // Will be calculated from geometry
    
    // Default OSM attributes
    lanes = 1;
    max_speed_kmh = 50;
    is_one_way = false;
    classification = "road";
}

void RoadRoute::parse_osm_tags() {
    // 1. Classification (Highway Type)
    if (tags.count("highway")) {
        classification = tags["highway"];
    }

    // 2. Lanes (Crucial for Capacity)
    if (tags.count("lanes")) {
        try {
            lanes = std::stoi(tags["lanes"]);
        } catch (...) { lanes = 1; }
    } else {
        // Infer lanes based on type if missing
        if (classification == "motorway") lanes = 2;
        else if (classification == "trunk") lanes = 2;
        else lanes = 1;
    }

    // 3. Max Speed (Crucial for Latency)
    if (tags.count("maxspeed")) {
        std::string speed_str = tags["maxspeed"];
        // Handle "100", "60 mph", "none"
        if (speed_str.find("mph") != std::string::npos) {
            try {
                int mph = std::stoi(speed_str.substr(0, speed_str.find(" ")));
                max_speed_kmh = (int)(mph * 1.60934);
            } catch (...) { max_speed_kmh = 50; }
        } else if (speed_str == "none") {
            max_speed_kmh = 130; // Autobahn style
        } else {
            try {
                max_speed_kmh = std::stoi(speed_str);
            } catch (...) { max_speed_kmh = 50; }
        }
    } else {
        // Default speeds based on type
        if (classification == "motorway") max_speed_kmh = 110;
        else if (classification == "trunk") max_speed_kmh = 90;
        else if (classification == "primary") max_speed_kmh = 80;
        else if (classification == "residential") max_speed_kmh = 30;
        else max_speed_kmh = 50;
    }

    // 4. Surface Quality
    if (tags.count("surface")) {
        std::string s = tags["surface"];
        if (s == "paved" || s == "asphalt" || s == "concrete") surface_quality = 1.0f;
        else if (s == "cobblestone" || s == "paving_stones") surface_quality = 0.8f;
        else if (s == "gravel" || s == "fine_gravel") surface_quality = 0.6f;
        else if (s == "dirt" || s == "earth" || s == "ground") surface_quality = 0.4f;
        else if (s == "sand") surface_quality = 0.2f;
    }

    // 5. One Way
    if (tags.count("oneway")) {
        is_one_way = (tags["oneway"] == "yes");
    }

    // --- CALCULATE DERIVED METRICS ---
    
    // Capacity: ~2000 vehicles per hour per lane is standard highway saturation flow
    float lane_capacity = (classification == "motorway" || classification == "trunk") ? 2000.0f : 1000.0f;
    base_capacity_vph = lanes * lane_capacity * surface_quality;

    // Weight Limits (Defaults)
    if (classification == "motorway" || classification == "trunk") max_weight_tons = 60.0f;
    else if (classification == "residential") max_weight_tons = 15.0f;
    else max_weight_tons = 40.0f;
}

void RoadRoute::update_metrics() {
    // Basic travel time calculation: T = Distance / Speed
    // We modify speed based on Surface Quality and Congestion (Flow/Capacity)
    
    if (length_km <= 0.001f) {
        length_km = get_length_km(); // Recalculate if not set
    }

    float speed_factor = surface_quality;
    
    // Congestion Penalty: If flow > 80% capacity, speed drops drastically
    float saturation = 0.0f;
    if (base_capacity_vph > 0) saturation = current_flow_vph / base_capacity_vph;
    
    if (saturation > 1.0f) speed_factor *= 0.1f; // Gridlock
    else if (saturation > 0.8f) speed_factor *= 0.5f; // Heavy traffic
    else if (saturation > 0.6f) speed_factor *= 0.8f; // Moderate

    float effective_speed = max_speed_kmh * speed_factor;
    if (effective_speed < 1.0f) effective_speed = 1.0f; // Prevent div/0

    travel_time_hours = length_km / effective_speed;
}