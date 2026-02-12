#include "RunwayChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

RunwayChokePoint::RunwayChokePoint(int id, std::string name, double lat, double lon, 
                                   float length, float width, std::string surface)
    : BaseChokePoint(id, name, "runway", lat, lon), 
      length_meters(length), 
      width_meters(width), 
      surface_type(surface) {
    
    // Defaults (CAVOK - Ceiling and Visibility OK)
    visibility_meters = 10000.0f; 
    friction_coefficient = 0.8f;   // 0.8 = Dry Asphalt (Perfect)
    crosswind_speed_kt = 0.0f;
    is_obstructed = false;
    is_maintenance = false;
}

void RunwayChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Weather Signal (METAR - METeorological Aerodrome Report)
    if (category == "weather") {
        if (sig.contains("visibility")) {
            visibility_meters = sig["visibility"].get<float>();
        }
        if (sig.contains("friction") || sig.contains("braking_action")) {
            // 0.8=Dry, 0.4=Wet, 0.2=Icy/Snow
            friction_coefficient = sig.value("friction", 0.8f);
        }
        if (sig.contains("crosswind")) {
            crosswind_speed_kt = sig["crosswind"].get<float>();
        }
    }
    
    // 2. Obstruction (Crash / Disabled Aircraft)
    else if (category == "obstruction" || category == "incident") {
        is_obstructed = sig.value("active", true);
    }

    // 3. Maintenance (Resurfacing / Rubber Removal)
    else if (category == "maintenance") {
        is_maintenance = sig.value("active", true);
    }

    last_update = sig.value("timestamp", 0LL);
}

float RunwayChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Hard Closures
    if (is_obstructed) return 0.0f;
    if (is_maintenance) return 0.0f;

    // 2. Crosswind Constraints
    // Heavy freighters (747-8F) limit is usually 30-35kt crosswind.
    // If friction is poor (ice), the limit drops to 15kt or less.
    float wind_limit = 35.0f * friction_coefficient; 
    if (crosswind_speed_kt > wind_limit) return 0.0f;

    // 3. Visibility Constraints (LVP - Low Visibility Procedures)
    // CAT I: Vis > 550m (Standard Ops)
    // CAT II: Vis > 300m (Reduced rate)
    // CAT III: Vis < 300m (Severe spacing required)
    
    float vis_factor = 1.0f;
    if (visibility_meters < 200.0f) {
        return 0.0f; // Effectively closed for commercial ops
    } 
    else if (visibility_meters < 550.0f) {
        // CAT II/III: Spacing increases massively to protect ILS signals
        vis_factor = 0.25f; 
    }
    else if (visibility_meters < 1200.0f) {
        // Marginal VFR / IFR: Slight reduction
        vis_factor = 0.7f;
    }

    // 4. Braking Action (Friction)
    // If friction is nil (ice), throughput is zero.
    // If friction is poor (rain/slush), braking distance increases -> lower frequency.
    float friction_factor = std::clamp(friction_coefficient / 0.8f, 0.1f, 1.0f);

    return vis_factor * friction_factor;
}

json RunwayChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"visibility", visibility_meters},
            {"friction", friction_coefficient},
            {"crosswind", crosswind_speed_kt},
            {"obstructed", is_obstructed}
        }},
        {"limits", {
            {"length", length_meters},
            {"surface", surface_type}
        }},
        {"throughput_mod", const_cast<RunwayChokePoint*>(this)->calculate_throughput_modifier()}
    };
}