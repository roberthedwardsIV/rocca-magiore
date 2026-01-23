#include "RailLine.hpp"
#include <algorithm>

RailLine::RailLine(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "rail_line";
    this->process_noise = 0.001f;
    current_state = {1.0f, 1.0f, 0.5f, 0.5f, 0LL};
}

void RailLine::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void RailLine::apply_signal(const json& sig) {
    current_state.unc_int += process_noise;
    current_state.unc_flow += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "integrity") {
        float z = 1.0f - severity;
        float K = current_state.unc_int / (current_state.unc_int + R);
        current_state.track_integrity += K * (z - current_state.track_integrity);
        current_state.unc_int *= (1.0f - K);
        
        if (current_state.track_integrity < 0.9f) {
            current_state.flow_capacity = std::min(current_state.flow_capacity, current_state.track_integrity);
        }
    }
    else if (category == "flow") {
        float z = 1.0f - severity;
        float K = current_state.unc_flow / (current_state.unc_flow + R);
        current_state.flow_capacity += K * (z - current_state.flow_capacity);
        current_state.unc_flow *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json RailLine::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"line_id", line_id},
        {"name", name},
        {"line_type", line_type},
        {"track_integrity", current_state.track_integrity},
        {"flow_capacity", current_state.flow_capacity},
        {"last_update", current_state.last_update}
    };
}