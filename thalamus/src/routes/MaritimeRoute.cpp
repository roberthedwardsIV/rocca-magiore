#include "MaritimeRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

MaritimeRoute::MaritimeRoute(long long id, std::string name)
    : BaseRoute(id, name, "maritime") {
    
    // Default Physical Constraints (Deep Ocean)
    max_draft_meters = 25.0f;     // Deep enough for VLCC/Capesize
    requires_ice_breaker = false;
    distance_nautical_miles = 0.0f;

    // Default Dynamic Conditions (Calm Seas)
    sea_state_level = 2.0f;       // Smooth (Wave height 0.1-0.5m)
    wind_speed_knots = 10.0f;     // Gentle Breeze
    
    // Default Security (Safe)
    threat_level = 0;             // 0=Safe
    threat_type = "none";
    insurance_premium_mult = 1.0f;

    // Default Operational Metrics
    effective_speed_knots = 18.0f;// Standard Container Ship Eco-Speed
    travel_time_hours = 0.0f;
    
    zone_type = "international";
    seamark_type = "fairway";
}

void MaritimeRoute::parse_osm_tags() {
    // 1. Seamark Type (Route Definition)
    if (tags.count("seamark:type")) {
        seamark_type = tags["seamark:type"];
    }

    // 2. Zone Type (Jurisdiction)
    if (tags.count("maritime")) {
        // e.g., maritime=yes, maritime=fairway
        zone_type = tags["maritime"];
    }

    // 3. Draft Limits (Depth)
    // OSM often uses "depth" or "seamark:fixme:depth"
    if (tags.count("depth")) {
        try {
            max_draft_meters = std::stof(tags["depth"]);
        } catch (...) { max_draft_meters = 25.0f; }
    }
    else if (tags.count("min_depth")) {
        try {
            max_draft_meters = std::stof(tags["min_depth"]);
        } catch (...) { max_draft_meters = 25.0f; }
    }

    // 4. Ice Conditions (Geography Check)
    // Simple heuristic: If latitude is extreme, flag for ice
    // (We would need geometry for this, assuming first point checks out)
    if (!geometry.empty()) {
        double lat = std::abs(geometry[0].lat);
        if (lat > 60.0) { // Arctic / Antarctic Circle approach
            requires_ice_breaker = true;
            // Default speed is much slower in ice zones
            effective_speed_knots = 8.0f; 
        }
    }

    // 5. Initial Static Threat Assessment (e.g., from tags)
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

void MaritimeRoute::update_metrics() {
    // 1. Calculate Distance in Nautical Miles (1 NM = 1.852 km)
    if (distance_nautical_miles <= 0.001f) {
        double km = get_length_km();
        distance_nautical_miles = km / 1.852;
    }

    // 2. Base Speed (Vessel Class Dependent - assuming Container Ship avg)
    float base_speed = 22.0f; // Max service speed
    if (requires_ice_breaker) base_speed = 10.0f;

    // 3. Weather Penalty (Sea State)
    // Douglas Scale:
    // 0-3: No effect
    // 4-5: Moderate (Slow down 10-20%)
    // 6-7: Rough (Slow down 40-60%)
    // 8-9: Survival Mode (Heave to / Speed ~0-5 knots)
    float weather_factor = 1.0f;
    if (sea_state_level >= 8.0f) weather_factor = 0.1f;
    else if (sea_state_level >= 6.0f) weather_factor = 0.5f;
    else if (sea_state_level >= 4.0f) weather_factor = 0.85f;

    // 4. Threat Penalty (Geopolitics)
    // Threat Level 0-2: No speed impact (maybe slight increase to clear zone)
    // Threat Level 3-4: Convoys required (Speed limited to convoy speed ~12-14 kn)
    // Threat Level 5: Route Closed
    float threat_factor = 1.0f;
    if (threat_level >= 5) {
        threat_factor = 0.0f; // Blockade
    } else if (threat_level >= 3) {
        // Convoy speed limit overrides base speed
        // If base * weather is > 14, cap it at 14.
        float convoy_speed = 14.0f; 
        if ((base_speed * weather_factor) > convoy_speed) {
            // Effectively reduces speed to convoy pace
            threat_factor = convoy_speed / (base_speed * weather_factor);
        }
        insurance_premium_mult = 10.0f; // War Risk Surcharge
    } else {
        insurance_premium_mult = 1.0f;
    }

    // 5. Final Calculation
    effective_speed_knots = base_speed * weather_factor * threat_factor;

    if (effective_speed_knots < 0.1f) {
        travel_time_hours = 99999.9f; // Effectively infinite / stuck
    } else {
        travel_time_hours = distance_nautical_miles / effective_speed_knots;
    }
}