#include "Highway.hpp"
#include <algorithm>
#include <cmath>

// Constructor
Highway::Highway(int id, std::string name) 
    : BaseSupplyLine(id, name, "highway") {
    
    this->process_noise = 0.005f; 
    
    current_state = {
        1.0f,       // Navigability
        0.9f,       // Maintenance (Default to Good condition)
        0.3f,       // Traffic Density (Light baseline)
        1.0f,       // Speed Modifier
        0.5f, 0.5f, 0.5f, // Uncertainties
        0LL
    };
}


// Packet Processor -> sends signal to apply_signal() with mutex locked
void Highway::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting highway state vector with Kalman logic
void Highway::apply_signal(const json& sig) {
    current_state.unc_nav += process_noise;
    current_state.unc_maint += process_noise;
    current_state.unc_traf += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "navigability") {
        float z = 1.0f - severity;
        float K = current_state.unc_nav / (current_state.unc_nav + R);
        current_state.navigability += K * (z - current_state.navigability);
        current_state.unc_nav *= (1.0f - K);
    }
    else if (category == "maintenance") {
        float z = 1.0f - severity; 
        float K = current_state.unc_maint / (current_state.unc_maint + R);
        current_state.maintenance_score += K * (z - current_state.maintenance_score);
        current_state.unc_maint *= (1.0f - K);
    } 
    else if (category == "traffic") {
        if (sig.contains("value")) {
            float z_traf = sig["value"].get<float>();
            float K = current_state.unc_traf / (current_state.unc_traf + R);
            current_state.traffic_density += K * (z_traf - current_state.traffic_density);
            current_state.unc_traf *= (1.0f - K);
        }
    }

    // Speed Calculation
    
    // Speed is capped by the worse of Navigability or Maintenance
    float road_condition_cap = current_state.navigability * current_state.maintenance_score;
    
    float traffic_friction = 1.0f;
    if (current_state.traffic_density > road_condition_cap) {
        // Congestion occurs when density exceeds the road's current effective capacity
        float overflow = current_state.traffic_density - road_condition_cap;
        traffic_friction = std::exp(-4.0f * overflow); 
    }

    current_state.transit_speed_modifier = road_condition_cap * traffic_friction;
    
    if (current_state.transit_speed_modifier < 0.0f) current_state.transit_speed_modifier = 0.0f;

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json Highway::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"line_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"navigability", current_state.navigability},
        {"maintenance_score", current_state.maintenance_score},
        {"traffic_density", current_state.traffic_density},
        {"transit_speed_modifier", current_state.transit_speed_modifier},
        {"last_update", current_state.last_update}
    };
}