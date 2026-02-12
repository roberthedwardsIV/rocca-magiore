#include "BridgeChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

// Constructor
BridgeChokePoint::BridgeChokePoint(int id, std::string name, double lat, double lon, 
                                   float weight_lim, float length, float clearance)
    : BaseChokePoint(id, name, "bridge", lat, lon), 
      max_weight_tons(weight_lim), 
      length_meters(length), 
      clearance_height_meters(clearance) {
    
    // --- PHYSICS LOGIC ---
    // Calculate the safe wind threshold based on geometry.
    // Base Safe Speed: 120 km/h (Hurricane force start)
    float base_limit = 120.0f; 

    // Penalty 1: Height (Wind shear increases with altitude)
    // -0.5 km/h for every meter of clearance above 10m
    float height_penalty = std::max(0.0f, (clearance_height_meters - 10.0f) * 0.5f);

    // Penalty 2: Span Length (Oscillation risk)
    // -1.0 km/h for every 100m of length
    float length_penalty = (length_meters / 100.0f) * 1.0f;

    max_wind_speed_kmh = std::max(60.0f, base_limit - height_penalty - length_penalty);
    
    // Initialize State
    structural_health = 1.0f;
    current_wind_speed = 0.0f;
    is_maintenance_active = false;
}

void BridgeChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Weather Signal (Real-time Wind)
    if (category == "weather") {
        if (sig.contains("wind_speed")) {
            current_wind_speed = sig["wind_speed"].get<float>();
        }
    }
    
    // 2. Seismic/Integrity Signal
    else if (category == "integrity" || category == "seismic") {
        float severity = sig.value("severity", 0.0f);
        // Bridges are rigid structures; damage accumulates and requires repair
        structural_health -= severity;
        if (structural_health < 0.0f) structural_health = 0.0f;
    }

    // 3. Maintenance/Closure Signal
    else if (category == "maintenance") {
        is_maintenance_active = sig.value("active", false);
    }

    last_update = sig.value("timestamp", 0LL);
}

float BridgeChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Catastrophic Structural Failure
    // If health is below 20%, the bridge is condemned/unsafe.
    if (structural_health < 0.2f) return 0.0f;

    // 2. Weather Closure (Binary Gate)
    // If wind exceeds our calculated physical limit, traffic stops.
    if (current_wind_speed > max_wind_speed_kmh) return 0.0f;

    // 3. Maintenance Throttle
    // Maintenance usually closes lanes, reducing flow to 50%
    if (is_maintenance_active) return 0.5f;

    // 4. Standard Operation
    // Flow is scaled by structural health (potholes/cracks slow traffic)
    return structural_health;
}

json BridgeChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"health", structural_health},
        {"wind_speed", current_wind_speed},
        {"wind_limit", max_wind_speed_kmh},
        {"dims", {
            {"len", length_meters},
            {"height", clearance_height_meters},
            {"weight_cap", max_weight_tons}
        }},
        {"is_closed", (current_wind_speed > max_wind_speed_kmh)},
        // Use const_cast to call non-const method inside const function (or make calc const)
        {"throughput_mod", const_cast<BridgeChokePoint*>(this)->calculate_throughput_modifier()}
    };
}