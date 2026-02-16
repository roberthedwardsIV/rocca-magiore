#include "PortCraneChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- CRANE PHYSICS CONSTANTS ---
    constexpr float BASE_WIND_LIMIT_KMH = 72.0f; // 40 knots (Stowage limit)
    constexpr float WIND_DERATE_START_KMH = 50.0f; // 27 knots (Slow down threshold)
    
    // Kinematics (Super Post-Panamax Baseline)
    constexpr float DEFAULT_HOIST_SPD = 1.5f;    // m/s (Loaded)
    constexpr float DEFAULT_TROLLEY_SPD = 3.5f;  // m/s
    constexpr float AVG_LIFT_HEIGHT = 20.0f;     // Meters (Deck to Quay)
    constexpr float TIME_SPREADER_LATCH = 30.0f; // Seconds (Twistlocks/Positioning)
}

PortCraneChokePoint::PortCraneChokePoint(int id, std::string name, double lat, double lon, 
                                         float swl, float outreach)
    : BaseChokePoint(id, name, "port_crane", lat, lon), 
      max_swl_tons(swl), 
      outreach_meters(outreach) {
    
    // Defaults
    hoist_speed_ms = DEFAULT_HOIST_SPD;
    trolley_speed_ms = DEFAULT_TROLLEY_SPD;
    
    current_wind_speed_kmh = 0.0f;
    mechanical_health = 1.0f;
    is_operational = true;
    is_in_use = false;
    
    dynamic_wind_limit_kmh = BASE_WIND_LIMIT_KMH;
    update_physics_limits();
}

void PortCraneChokePoint::update_physics_limits() {
    // 1. Calculate Theoretical Cycle Time
    // Avg trolley distance is roughly half the outreach
    float dist_trolley = outreach_meters * 0.5f;
    
    float t_hoist = (AVG_LIFT_HEIGHT / hoist_speed_ms) * 2.0f; // Up + Down
    float t_travel = (dist_trolley / trolley_speed_ms) * 2.0f; // Out + In
    
    // Total cycle
    cycle_time_seconds = t_hoist + t_travel + TIME_SPREADER_LATCH;
    
    // 2. Adjust Wind Limit based on Geometry (Larger cranes = more wind moment)
    // Taller/Longer cranes are less stable.
    // Penalty: -1 km/h limit per 5m outreach beyond 40m
    float size_penalty = std::max(0.0f, (outreach_meters - 40.0f) / 5.0f);
    dynamic_wind_limit_kmh = BASE_WIND_LIMIT_KMH - size_penalty;
}

void PortCraneChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Weather
    if (category == "weather") {
        if (sig.contains("wind_speed_kmh")) {
            current_wind_speed_kmh = sig["wind_speed_kmh"].get<float>();
        } else if (sig.contains("wind_speed_ms")) {
            current_wind_speed_kmh = sig["wind_speed_ms"].get<float>() * 3.6f;
        }
    }
    
    // 2. Mechanical Health
    else if (category == "mechanical" || category == "integrity") {
        float damage = sig.value("severity", 0.0f);
        mechanical_health -= damage;
        if (mechanical_health < 0.0f) mechanical_health = 0.0f;
        
        // Critical Failure
        if (mechanical_health < 0.4f) is_operational = false;
    }

    // 3. Operations
    else if (category == "ops") {
        if (sig.contains("operational")) is_operational = sig["operational"].get<bool>();
        if (sig.contains("in_use")) is_in_use = sig["in_use"].get<bool>();
    }

    last_update = sig.value("timestamp", 0LL);
}

float PortCraneChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Hard Stops
    if (!is_operational) return 0.0f;
    if (current_wind_speed_kmh > dynamic_wind_limit_kmh) return 0.0f; // Winded off

    // 2. Wind Derating (Safety Slowdown)
    // Between 'Start Derate' (50kmh) and 'Limit' (72kmh), speed drops linearly.
    // Operators move slower to control sway.
    float wind_factor = 1.0f;
    if (current_wind_speed_kmh > WIND_DERATE_START_KMH) {
        float range = dynamic_wind_limit_kmh - WIND_DERATE_START_KMH;
        float excess = current_wind_speed_kmh - WIND_DERATE_START_KMH;
        wind_factor = 1.0f - (excess / range);
        if (wind_factor < 0.0f) wind_factor = 0.0f;
    }

    // 3. Mechanical Efficiency
    // A worn crane moves slower or suffers micro-stoppages.
    return mechanical_health * wind_factor;
}

json PortCraneChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"operational", is_operational},
            {"wind_speed", current_wind_speed_kmh},
            {"in_use", is_in_use}
        }},
        {"physics", {
            {"wind_limit", dynamic_wind_limit_kmh},
            {"cycle_time_s", cycle_time_seconds},
            {"swl_tons", max_swl_tons}
        }},
        {"health", mechanical_health},
        {"throughput_mod", const_cast<PortCraneChokePoint*>(this)->calculate_throughput_modifier()}
    };
}