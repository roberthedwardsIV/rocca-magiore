#include "CanalLock.hpp"
#include <algorithm>

CanalLock::CanalLock(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "canal_lock";
    this->process_noise = 0.004f; // Locks are mechanical and prone to wear/outages
    current_state = {1.0f, 1.0f, 0.5f, 0.5f, 0LL};
}

void CanalLock::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void CanalLock::apply_signal(const json& sig) {
    current_state.unc_mech += process_noise;
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
        
        // Logical coupling: Mechanical failure forces a capacity drop
        if (current_state.mechanical_health < 0.5f) {
            current_state.transit_capacity = std::min(current_state.transit_capacity, current_state.mechanical_health);
        }
    } 
    else if (category == "capacity") {
        float z = 1.0f - severity; // Severity 1.0 = Halted transit
        float K = current_state.unc_cap / (current_state.unc_cap + R);
        current_state.transit_capacity += K * (z - current_state.transit_capacity);
        current_state.unc_cap *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json CanalLock::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"lock_id", line_id},
        {"name", name},
        {"mechanical_health", current_state.mechanical_health},
        {"transit_capacity", current_state.transit_capacity},
        {"last_update", current_state.last_update}
    };
}