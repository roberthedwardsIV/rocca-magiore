#include "TunnelChokePoint.hpp"
#include <iostream>
#include <algorithm>

TunnelChokePoint::TunnelChokePoint(int id, std::string name, double lat, double lon, float length)
    : BaseChokePoint(id, name, "tunnel", lat, lon), length_meters(length) {
    
    // Defaults
    structural_health = 1.0f;
    is_maintenance_active = false;
    is_flooded = false;
    ventilation_active = true;
    
    // Tunnels are generally more resilient to shaking than bridges, but harder to fix
    seismic_rating = 0.8f; 
}

void TunnelChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Environmental Hazards (Gas/Water)
    if (category == "env_hazard") {
        std::string type = sig.value("type", "");
        
        if (type == "flood") {
            // Flooding is usually binary and persistent until "cleared" signal
            is_flooded = sig.value("active", true);
        }
        else if (type == "vent_fail" || type == "gas") {
            // If gas is high or vents fail, we lose active ventilation
            ventilation_active = !sig.value("active", true);
        }
    }
    
    // 2. Seismic Signal
    else if (category == "integrity" || category == "seismic") {
        float raw_severity = sig.value("severity", 0.0f);
        
        // Apply resistance rating. A rating of 1.0 means immune. 0.0 means naked.
        float damage = raw_severity * (1.0f - seismic_rating);
        
        structural_health -= damage;
        if (structural_health < 0.0f) structural_health = 0.0f;
    }

    // 3. Maintenance Signal
    else if (category == "maintenance") {
        is_maintenance_active = sig.value("active", false);
    }

    last_update = sig.value("timestamp", 0LL);
}

float TunnelChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Critical Obstruction (Flooding / Collapse)
    if (is_flooded) return 0.0f;
    if (structural_health < 0.2f) return 0.0f;

    // 2. Ventilation Failure (The Piston Effect)
    // If vents fail in a LONG tunnel (>1km), traffic must stop or crawl to avoid suffocation.
    // Short tunnels might rely on natural airflow.
    if (!ventilation_active) {
        if (length_meters > 1000.0f) return 0.0f; // Forced closure
        else return 0.2f; // Slow crawl allowed
    }

    // 3. Maintenance Throttle
    // Tunnels usually close one bore/lane at a time.
    if (is_maintenance_active) return 0.5f;

    // 4. Standard Operation
    // Structural damage (cracks, liner issues) slows traffic for safety
    return structural_health;
}

json TunnelChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"health", structural_health},
        {"length", length_meters},
        {"status", {
            {"flooded", is_flooded},
            {"ventilation", ventilation_active},
            {"maintenance", is_maintenance_active}
        }},
        {"throughput_mod", const_cast<TunnelChokePoint*>(this)->calculate_throughput_modifier()}
    };
}