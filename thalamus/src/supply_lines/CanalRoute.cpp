#include "CanalRoute.hpp"
#include <algorithm>
#include <cmath>

// Constructor
CanalRoute::CanalRoute(int id, std::string name) 
    : BaseSupplyLine(id, name, "canal_route") {
    
    this->process_noise = 0.002f; 
    
    current_state = {
        1.0f, 1.0f,  
        0.5f,        
        12.0f,       
        0.5f, 0.5f, 0.5f, 
        0LL
    };
}


// Packet processor -> sends to apply_signal() with mutex locked
void CanalRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting canal route state vector with Kalman logic
void CanalRoute::apply_signal(const json& sig) {
    current_state.unc_nav += process_noise;
    current_state.unc_depth += process_noise;
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
    else if (category == "depth") {
        float z = 1.0f - severity;
        float K = current_state.unc_depth / (current_state.unc_depth + R);
        current_state.depth_health += K * (z - current_state.depth_health);
        current_state.unc_depth *= (1.0f - K);
    }
    else if (category == "traffic") {
        if (sig.contains("value")) {
            float z_traf = sig["value"].get<float>();
            float K = current_state.unc_traf / (current_state.unc_traf + R);
            current_state.traffic_density += K * (z_traf - current_state.traffic_density);
            current_state.unc_traf *= (1.0f - K);
        }
    }

    // Transit Time Calculation
    float base_time = 12.0f;
    
    if (current_state.navigability < 0.2f) {
        current_state.transit_time_hours = 999.0f; // Effectively closed
    } 
    else {
        float congestion = current_state.traffic_density / (current_state.navigability + 0.01f);
        
        if (congestion > 0.8f) {
            float delay_factor = std::exp((congestion - 0.8f) * 2.0f);
            current_state.transit_time_hours = base_time * delay_factor;
        } else {
            current_state.transit_time_hours = base_time;
        }

        if (current_state.depth_health < 0.9f) {
            current_state.transit_time_hours += 4.0f; // Slower speeds required
        }
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json CanalRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"route_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"navigability", current_state.navigability},
        {"depth_health", current_state.depth_health},
        {"traffic_density", current_state.traffic_density},
        {"transit_time_hours", current_state.transit_time_hours},
        {"last_update", current_state.last_update}
    };
}