#include "MaritimePort.hpp"

MaritimePort::MaritimePort(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "maritime_port";
    this->process_noise = 0.004f;
    current_state = {1.0f, 0.3f, 1.0f, 0.5f, 0.5f, 0.5f, 0LL};
}

void MaritimePort::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void MaritimePort::apply_signal(const json& sig) {
    current_state.unc_crane += process_noise;
    current_state.unc_util += process_noise;
    current_state.unc_yard += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "equipment") {
        float z = 1.0f - severity;
        float K = current_state.unc_crane / (current_state.unc_crane + R);
        current_state.crane_health += K * (z - current_state.crane_health);
        current_state.unc_crane *= (1.0f - K);
    }
    else if (category == "utilization") {
        float K = current_state.unc_util / (current_state.unc_util + R);
        current_state.berth_utilization += K * (severity - current_state.berth_utilization);
        current_state.unc_util *= (1.0f - K);
    }
    else if (category == "yard") {
        float z = 1.0f - severity;
        float K = current_state.unc_yard / (current_state.unc_yard + R);
        current_state.yard_throughput += K * (z - current_state.yard_throughput);
        current_state.unc_yard *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json MaritimePort::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"port_id", line_id},
        {"name", name},
        {"crane_health", current_state.crane_health},
        {"berth_utilization", current_state.berth_utilization},
        {"yard_throughput", current_state.yard_throughput},
        {"last_update", current_state.last_update}
    };
}