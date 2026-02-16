#include "MaritimeRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

// Constructor for initialization
MaritimeRoute::MaritimeRoute(long long id, std::string name)
    : BaseRoute(id, name, "maritime") {
    
    // Default values
    max_draft_meters = 25.0f;
    requires_ice_breaker = false;
    distance_nautical_miles = 0.0f;
    design_speed_knots = 18.0f;    
    zone_type = "international"; 
    seamark_type = "fairway"; 
    sea_state_level = 2.0f;
    wave_height_meters = 0.5f;     
    wind_component_knots = 0.0f;
    ice_coverage_pct = 0.0f;
    threat_level = 0; 
    threat_type = "none";
    insurance_premium_mult = 1.0f;
    effective_speed_knots = design_speed_knots;
    travel_time_hours = 0.0f; 
    fuel_efficiency_multiplier = 1.0f;
}

// Direct updates to static fields from OSM tags
void MaritimeRoute::parse_osm_tags() {
    if (tags.count("seamark:type")) seamark_type = tags["seamark:type"];
    if (tags.count("maritime")) zone_type = tags["maritime"];

    if (tags.count("depth")) {
        try { max_draft_meters = std::stof(tags["depth"]); } catch (...) { max_draft_meters = 25.0f;} 
    }
    else if (tags.count("min_depth")) {
        try { max_draft_meters = std::stof(tags["min_depth"]); } catch (...) { max_draft_meters = 25.0f; } 
    }

    if (!geometry.empty()) {
        double lat = std::abs(geometry[0].lat);
        if (lat > 60.0) requires_ice_breaker = true;
    }

    if (tags.count("hazard")) {
        std::string h = tags["hazard"];
        if (h == "piracy") {
            threat_level = 3;
            threat_type = "piracy";
        } else if (h == "military" || h == "danger_area") {
            threat_level = 2;
            threat_type = "military_exercise";
        }
    }
}

// Updates to dynamic + calculated fields from Thalamus signaling
void MaritimeRoute::update_metrics() {
    // Distance
    if (distance_nautical_miles <= 0.001f) {
        double km = get_length_km();
        distance_nautical_miles = km / 1.852;
    }

    // Hydrodynamics
    const float k_wave = 0.012f;
    float wave_speed_loss = design_speed_knots * (k_wave * std::pow(wave_height_meters, 2.0f));
    float wind_speed_loss = wind_component_knots / 20.0f;
    float weather_speed = design_speed_knots - wave_speed_loss - wind_speed_loss;
    if (weather_speed < 2.0f) weather_speed = 2.0f; 

    // Ice Constraints 
    if (ice_coverage_pct > 0.1f) {
        requires_ice_breaker = true;
        float ice_speed = 12.0f * (1.0f - ice_coverage_pct);
        if (ice_speed < 3.0f) ice_speed = 3.0f; 
        weather_speed = std::min(weather_speed, ice_speed);
    } else if (requires_ice_breaker) {
        weather_speed = std::min(weather_speed, 12.0f);
    }

    // Threat Constraints
    float threat_speed_limit = 999.0f;
    if (threat_level >= 5) {
        threat_speed_limit = 0.0f; 
    } else if (threat_level >= 3) {
        threat_speed_limit = 14.0f;
        insurance_premium_mult = 10.0f; 
    } else {
        insurance_premium_mult = 1.0f;
    }

    effective_speed_knots = std::min(weather_speed, threat_speed_limit);
    
    if (effective_speed_knots > 0.0f && effective_speed_knots < design_speed_knots) {
        float resistance_factor = 1.0f + (k_wave * std::pow(wave_height_meters, 2.0f));
        fuel_efficiency_multiplier = resistance_factor;
    } else {
        fuel_efficiency_multiplier = 1.0f;
    }

    // Latency
    if (effective_speed_knots > 0.1f) {
        travel_time_hours = distance_nautical_miles / effective_speed_knots;
    } else {
        travel_time_hours = 99999.9f; 
    }
}

void MaritimeRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::string category = sig.value("category", "none");

    if (category == "weather" || category == "sea_state") {
        if (sig.contains("wave_height_m")) {
            wave_height_meters = sig["wave_height_m"].get<float>();
            if (wave_height_meters < 0.1f) sea_state_level = 0;
            else if (wave_height_meters < 0.5f) sea_state_level = 2;
            else if (wave_height_meters < 1.25f) sea_state_level = 3;
            else if (wave_height_meters < 2.5f) sea_state_level = 4;
            else if (wave_height_meters < 4.0f) sea_state_level = 5;
            else if (wave_height_meters < 6.0f) sea_state_level = 6;
            else if (wave_height_meters < 9.0f) sea_state_level = 7;
            else if (wave_height_meters < 14.0f) sea_state_level = 8;
            else sea_state_level = 9;
        }
        if (sig.contains("wind_headwind_knots")) {
            wind_component_knots = sig["wind_headwind_knots"].get<float>();
        }
    }
    else if (category == "threat" || category == "security") {
        if (sig.contains("level")) threat_level = sig["level"].get<int>();
        if (sig.contains("type")) threat_type = sig["type"].get<std::string>();
    }
    else if (category == "ice") {
        if (sig.contains("coverage_pct")) ice_coverage_pct = sig["coverage_pct"].get<float>();
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

json MaritimeRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(route_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"sea_state", sea_state_level},
        {"wave_height_m", wave_height_meters},
        {"threat_level", threat_level},
        {"ice_coverage", ice_coverage_pct},
        {"fuel_efficiency_mult", fuel_efficiency_multiplier},
        {"effective_speed_kts", effective_speed_knots},
        {"travel_time_h", travel_time_hours},
        {"insurance_mult", insurance_premium_mult},
        {"last_update", last_update}
    };
}