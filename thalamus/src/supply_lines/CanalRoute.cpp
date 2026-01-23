#include "CanalRoute.hpp"
#include <algorithm>

CanalRoute::CanalRoute(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "canal_route";
    this->process_noise = 0.002f; 
    current_state = {1.0f, 1.0f, 0.5f, 0.5f, 0LL};
}

void CanalRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void CanalRoute::apply_signal(const json& sig) {
    current_state.unc_nav += process_noise;
    current_state.unc_depth += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "navigability") {
        float z = 1.0f - severity; // Severity 1.0 = Grounding/Blockage
        float K = current_state.unc_nav / (current_state.unc_nav + R);
        current_state.navigability += K * (z - current_state.navigability);
        current_state.unc_nav *= (1.0f - K);
    } 
    else if (category == "depth") {
        float z = 1.0f - severity; // Severity 1.0 = Draught restriction
        float K = current_state.unc_depth / (current_state.unc_depth + R);
        current_state.depth_health += K * (z - current_state.depth_health);
        current_state.unc_depth *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json CanalRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"route_id", line_id},
        {"name", name},
        {"navigability", current_state.navigability},
        {"depth_health", current_state.depth_health},
        {"last_update", current_state.last_update}
    };
}