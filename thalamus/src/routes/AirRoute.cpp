#include "AirRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // Baseline: Boeing 777-300ER (Heavy Long Haul)
    constexpr float CRUISE_MACH = 0.84f;            
    constexpr float TURBULENCE_PENETRATION_MACH = 0.78f; 
    constexpr float FUEL_BURN_KG_PER_HOUR = 7500.0f; 
    constexpr float SPEED_OF_SOUND_KTS_SL = 661.47f; 
    constexpr int OPTIMAL_CRUISE_FL = 370;          
    constexpr float FUEL_PENALTY_PER_1000FT = 0.03f; 
    constexpr float ETOPS_THRESHOLD_NM = 420.0f;    
    constexpr int ETOPS_RATING_DEFAULT = 180;       
}

// Constructor for initialization
AirRoute::AirRoute(long long id, std::string name)
    : BaseRoute(id, name, "air") {
    
    // Default values
    distance_nm = 0.0f;
    min_flight_level = 180;    
    max_flight_level = 450;    
    etops_required = false;
    etops_rating_minutes = 0;
    wind_component_knots = 0.0f; 
    turbulence_index = 0.0f;     
    is_icing_conditions = false;
    is_conflict_zone = false;
    is_closed = false;
    effective_ground_speed_kts = 480.0f;
    estimated_fuel_burn_kg = 0.0f;
}


// Direct updates to static fields from OSM tags
void AirRoute::parse_osm_tags() {
    if (tags.count("ref")) name = tags["ref"];

    // Altitude Constraints 
    if (tags.count("min_altitude")) {
        try {
            std::string alt = tags["min_altitude"];
            if (alt.substr(0, 2) == "FL") min_flight_level = std::stoi(alt.substr(2));
            else min_flight_level = std::stoi(alt) / 100;
        } catch (...) { min_flight_level = 180; }
    }
    
    if (tags.count("max_altitude")) {
        try {
            std::string alt = tags["max_altitude"];
            if (alt.substr(0, 2) == "FL") max_flight_level = std::stoi(alt.substr(2));
            else max_flight_level = std::stoi(alt) / 100;
        } catch (...) { max_flight_level = 450; } // Default service ceiling
    }

    if (tags.count("usage")) {
        if (tags["usage"] == "military") is_closed = true; 
    }
    
    update_metrics();
}


// Updates to dynamic + calculated fields from Thalamus signaling
void AirRoute::update_metrics() {
    // Distance Calculation
    if (distance_nm <= 0.1f) {
        distance_nm = get_length_km() * 0.539957;
    }

    // ETOPS Check
    if (distance_nm > ETOPS_THRESHOLD_NM) {
        etops_required = true;
        etops_rating_minutes = ETOPS_RATING_DEFAULT;
    }

    // Conflict Zone Logic
    if (is_conflict_zone || is_closed) {
        effective_ground_speed_kts = 0.0f;
        estimated_fuel_burn_kg = 0.0f;
        return; 
    }

    // True Airspeed (TAS) Calculation
    float target_mach = CRUISE_MACH;
    if (turbulence_index > 0.5f) {
        target_mach = TURBULENCE_PENETRATION_MACH;
    }

    // TAS approx 573 kts at FL350.
    float true_air_speed = target_mach * 573.0f; 

    // Ground Speed Calculation 
    effective_ground_speed_kts = true_air_speed + wind_component_knots;

    // Jetstream Safety Floor
    if (effective_ground_speed_kts < 300.0f) {
        effective_ground_speed_kts = 250.0f; // Severe headwind penalty
    }

    // Fuel Burn 
    if (effective_ground_speed_kts > 10.0f) {
        float flight_time_hours = distance_nm / effective_ground_speed_kts;
        
        // Calculate Fuel Efficiency Factor based on Altitude
        int cruise_fl = std::min(OPTIMAL_CRUISE_FL, max_flight_level);
        
        // If route forces us lower than optimal, apply penalty
        float altitude_penalty = 1.0f;
        if (cruise_fl < OPTIMAL_CRUISE_FL) {
            int deficit_thousands = (OPTIMAL_CRUISE_FL - cruise_fl) / 10;
            altitude_penalty += (deficit_thousands * FUEL_PENALTY_PER_1000FT);
        }

        // Apply Icing Penalty
        float icing_penalty = is_icing_conditions ? 1.08f : 1.0f;

        // Final Calculation
        estimated_fuel_burn_kg = flight_time_hours * FUEL_BURN_KG_PER_HOUR * altitude_penalty * icing_penalty;
    }
}


// Main signal routing function
void AirRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::string category = sig.value("category", "none");

    if (category == "weather") {
        if (sig.contains("headwind")) {
            wind_component_knots = -1.0f * sig["headwind"].get<float>();
        } else if (sig.contains("tailwind")) {
            wind_component_knots = sig["tailwind"].get<float>();
        }
        
        if (sig.contains("turbulence")) {
            turbulence_index = sig["turbulence"].get<float>();
        }
        
        if (sig.contains("icing")) {
            is_icing_conditions = sig["icing"].get<bool>();
        }
    }

    else if (category == "security" || category == "notam") {
        bool closed = sig.value("closed", false);
        if (closed) {
            is_conflict_zone = true;
            is_closed = true;
        } else {
            is_conflict_zone = false;
            is_closed = false;
        }
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}


// JSON packager for archival
json AirRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(route_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"wind_comp", wind_component_knots},
        {"is_closed", is_closed},
        {"etops", etops_required},
        {"ground_speed", effective_ground_speed_kts},
        {"fuel_burn_kg", estimated_fuel_burn_kg},
        {"flight_levels", {min_flight_level, max_flight_level}},
        {"turbulence", turbulence_index},
        {"last_update", last_update}
    };
}