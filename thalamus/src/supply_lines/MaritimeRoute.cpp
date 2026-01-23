#include "MaritimeRoute.hpp"

MaritimeRoute::MaritimeRoute(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "maritime_route";
    this->process_noise = 0.007f;
    current_state = {1.0f, 0.0f, 0.5f, 0.5f, 0LL};
}

void MaritimeRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void MaritimeRoute::apply_signal(const json& sig) {
    current_state.unc_nav += process_noise;
    current_state.unc_risk += process_noise;

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
    else if (category == "risk") {
        float K = current_state.unc_risk / (current_state.unc_risk + R);
        current_state.risk_index += K * (severity - current_state.risk_index);
        current_state.unc_risk *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json MaritimeRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"route_id", line_id},
        {"name", name},
        {"navigability", current_state.navigability},
        {"risk_index", current_state.risk_index},
        {"last_update", current_state.last_update}
    };
}