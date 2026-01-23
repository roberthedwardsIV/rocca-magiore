#include "RailYard.hpp"

RailYard::RailYard(int id, std::string name) {
    this->line_id = id; 
    this->name = name;
    this->line_type = "rail_yard";
    this->process_noise = 0.005f; 
    current_state = {1.0f, 0.2f, 0.5f, 0.5f, 0LL};
}

void RailYard::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void RailYard::apply_signal(const json& sig) {
    current_state.unc_proc += process_noise;
    current_state.unc_util += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "processing") {
        float z = 1.0f - severity;
        float K = current_state.unc_proc / (current_state.unc_proc + R);
        current_state.processing_health += K * (z - current_state.processing_health);
        current_state.unc_proc *= (1.0f - K);
    }
    else if (category == "utilization") {
        float K = current_state.unc_util / (current_state.unc_util + R);
        current_state.utilization += K * (severity - current_state.utilization);
        current_state.unc_util *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json RailYard::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"yard_id", line_id},
        {"name", name},
        {"line_type", line_type},
        {"processing_health", current_state.processing_health},
        {"utilization", current_state.utilization},
        {"last_update", current_state.last_update}
    };
}