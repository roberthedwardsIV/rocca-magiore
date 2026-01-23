#include "Highway.hpp"
#include <algorithm>

Highway::Highway(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "highway";
    
    this->process_noise = 0.005f; 
    
    current_state = {1.0f, 1.0f, 0.5f, 0.5f, 0LL};
}

void Highway::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void Highway::apply_signal(const json& sig) {
    current_state.unc_nav += process_noise;
    current_state.unc_flow += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "navigability") {
        float z = 1.0f - severity;
        float K = current_state.unc_nav / (current_state.unc_nav + R);
        current_state.navigability += K * (z - current_state.navigability);
        current_state.unc_nav *= (1.0f - K);
        
        // If road is physically compromised, traffic flow is capped
        if (current_state.navigability < 1.0f) {
            current_state.traffic_flow = std::min(current_state.traffic_flow, current_state.navigability);
        }
    } 
    else if (category == "traffic") {
        float z = 1.0f - severity; 
        float K = current_state.unc_flow / (current_state.unc_flow + R);
        current_state.traffic_flow += K * (z - current_state.traffic_flow);
        current_state.unc_flow *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json Highway::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"line_id", line_id},
        {"name", name},
        {"line_type", line_type},
        {"navigability", current_state.navigability},
        {"traffic_flow", current_state.traffic_flow},
        {"last_update", current_state.last_update}
    };
}