#include "PipelineLine.hpp"

PipelineLine::PipelineLine(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "pipeline_line";
    this->process_noise = 0.0001f; 
    current_state = {1.0f, 1.0f, 0.1f, 0.1f, 0LL};
}

void PipelineLine::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void PipelineLine::apply_signal(const json& sig) {
    current_state.unc_int += process_noise;
    current_state.unc_flow += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "integrity") {
        float z = 1.0f - severity;
        float K = current_state.unc_int / (current_state.unc_int + R);
        current_state.containment_integrity += K * (z - current_state.containment_integrity);
        current_state.unc_int *= (1.0f - K);
        
        // Rupture (low integrity) causes instant flow loss
        if (current_state.containment_integrity < 0.7f) {
            current_state.flow_rate = std::min(current_state.flow_rate, current_state.containment_integrity);
        }
    }
    else if (category == "flow") {
        float z = 1.0f - severity;
        float K = current_state.unc_flow / (current_state.unc_flow + R);
        current_state.flow_rate += K * (z - current_state.flow_rate);
        current_state.unc_flow *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json PipelineLine::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"segment_id", line_id},
        {"name", name},
        {"containment_integrity", current_state.containment_integrity},
        {"flow_rate", current_state.flow_rate},
        {"last_update", current_state.last_update}
    };
}