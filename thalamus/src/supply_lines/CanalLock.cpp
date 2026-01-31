#include "CanalLock.hpp"
#include <algorithm>
#include <cmath>

// Constructor
CanalLock::CanalLock(int id, std::string name) 
    : BaseSupplyLine(id, name, "canal_lock") {
    
    this->process_noise = 0.004f; 
    
    current_state = {
        1.0f, 1.0f,  
        1.0f, 0.2f,  
        0.5f, 0.5f, 0.5f, 
        0LL
    };
}


// Packet processor -> sends to apply_signal() with mutex locked
void CanalLock::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting canal lock state vector with Kalman logic
void CanalLock::apply_signal(const json& sig) {
    current_state.unc_mech += process_noise;
    current_state.unc_water += process_noise;
    current_state.unc_cap += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "mechanical") {
        float z = 1.0f - severity;
        float K = current_state.unc_mech / (current_state.unc_mech + R);
        current_state.mechanical_health += K * (z - current_state.mechanical_health);
        current_state.unc_mech *= (1.0f - K);

        if (current_state.mechanical_health < 0.6f) {
            current_state.transit_capacity = 0.0f; // Hard Stop
            current_state.queue_size += 0.2f; // Backlog spikes immediately
        }
    } 
    else if (category == "environmental") {
        float z = 1.0f - severity;
        float K = current_state.unc_water / (current_state.unc_water + R);
        current_state.water_level += K * (z - current_state.water_level);
        current_state.unc_water *= (1.0f - K);

        if (current_state.water_level < 0.8f) {
            // Capacity drops linearly with water level below 80%
            current_state.transit_capacity = std::min(current_state.transit_capacity, current_state.water_level);
        }
    }
    else if (category == "throughput") {
         if (sig.contains("value")) {
            float z_cap = sig["value"].get<float>();
            float K = current_state.unc_cap / (current_state.unc_cap + R);
            current_state.transit_capacity += K * (z_cap - current_state.transit_capacity);
            current_state.unc_cap *= (1.0f - K);
        }
    }

    if (current_state.queue_size > 1.0f) current_state.queue_size = 1.0f;
    
    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json CanalLock::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"lock_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"mechanical_health", current_state.mechanical_health},
        {"water_level", current_state.water_level},
        {"transit_capacity", current_state.transit_capacity},
        {"queue_size", current_state.queue_size},
        {"last_update", current_state.last_update}
    };
}