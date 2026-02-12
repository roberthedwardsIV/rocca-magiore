#include "AirRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

AirRoute::AirRoute(long long id, std::string name)
    : BaseRoute(id, name, "air") {
    
    // Default Geometry (Generic Airway)
    distance_nm = 0.0f;
    min_flight_level = 180;    // FL180 (Class A Airspace start in many places)
    max_flight_level = 450;    // FL450 (Service ceiling for most commercial jets)

    // Default Dynamic Weather (Standard Day)
    wind_component_knots = 0.0f; // Calm
    turbulence_index = 0.0f;     // Smooth
    is_icing_conditions = false;

    // Default Regulatory & Risk
    etops_required = false;
    etops_rating_minutes = 0;
    is_conflict_zone = false;
    is_closed = false;

    // Operational Defaults (Boeing 777-300ER baseline)
    effective_ground_speed_kts = 490.0f; // roughly Mach 0.84
    estimated_fuel_burn_kg = 0.0f;
}

void AirRoute::parse_osm_tags() {
    // 1. Airway Reference (e.g., "J37", "V16")
    if (tags.count("ref")) {
        name = tags["ref"]; // Airways are usually known by ref, not name
    }

    // 2. Altitude Limits (Flight Levels)
    // OSM tags: "min_altitude", "max_altitude" (often in feet or FL)
    if (tags.count("min_altitude")) {
        try {
            // Check if "FL" prefix exists
            std::string alt = tags["min_altitude"];
            if (alt.substr(0, 2) == "FL") {
                min_flight_level = std::stoi(alt.substr(2));
            } else {
                // Assume feet, convert to FL (divide by 100)
                min_flight_level = std::stoi(alt) / 100;
            }
        } catch (...) {}
    }

    // 3. Usage / Type
    if (tags.count("usage")) {
        if (tags["usage"] == "military") is_closed = true; // Civil traffic blocked
    }
}

void AirRoute::update_metrics() {
    // 1. Calculate Distance (if not set)
    if (distance_nm <= 0.1f) {
        // Convert KM to NM
        distance_nm = get_length_km() * 0.539957;
    }

    // 2. ETOPS Check (Heuristic)
    // If route is long (> 400 NM) and likely oceanic (no way to know for sure without map, 
    // but we assume long segments are over water/deserts).
    if (distance_nm > 400.0f) {
        etops_required = true;
        etops_rating_minutes = 120; // Default ETOPS-120
    }

    // 3. Conflict Zone Logic
    if (is_conflict_zone) {
        is_closed = true; 
        effective_ground_speed_kts = 0.0f;
        return; 
    }

    // 4. Ground Speed Calculation
    // Base True Airspeed (TAS) for a widebody jet ~480-500 kts
    float true_air_speed = 490.0f; 

    // Adjust for Turbulence (Pilots slow to Va/Vb penetration speed)
    if (turbulence_index > 0.5f) {
        true_air_speed = 280.0f; // Slow down significantly for severe turbulence
    }

    // Ground Speed = TAS + Wind (Headwind is negative)
    effective_ground_speed_kts = true_air_speed + wind_component_knots;

    // Safety floor (Simulates "cannot fly against 200kt jetstream")
    if (effective_ground_speed_kts < 300.0f) {
        // Technically still flying, but economically unviable
        // or effectively "closed" due to range limits.
    }

    // 5. Fuel Burn Estimation
    // Heavy Jet (777/A350) burns approx 7,500 kg/hour
    // Time = Distance / Speed
    if (effective_ground_speed_kts > 10.0f) {
        float flight_time_hours = distance_nm / effective_ground_speed_kts;
        float burn_rate = 7500.0f; // kg per hour
        
        estimated_fuel_burn_kg = flight_time_hours * burn_rate;
    } else {
        estimated_fuel_burn_kg = 0.0f; // Grounded
    }
}