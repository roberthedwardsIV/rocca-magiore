#include "PipelineStation.hpp"

PipelineStation::PipelineStation(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "pipeline_station";
    this->process_noise = 0.003f; 
    current_state = {1.0f, 1.0f, 0.5f, 0.5f, 0LL};
}

void PipelineStation::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void PipelineStation::apply_signal(const json& sig) {
    current_state.unc_pump += process_noise;
    current_state.unc_pres += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "mechanical") {
        float z = 1.0f - severity;
        float K = current_state.unc_pump / (current_state.unc_pump + R);
        current_state.pump_efficiency += K * (z - current_state.pump_efficiency);
        current_state.unc_pump *= (1.0f - K);
    }
    else if (category == "pressure") {
        float z = 1.0f - severity; // Severity 1.0 = Pressure Loss
        float K = current_state.unc_pres / (current_state.unc_pres + R);
        current_state.pressure_gradient += K * (z - current_state.pressure_gradient);
        current_state.unc_pres *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json PipelineStation::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"station_id", line_id},
        {"name", name},
        {"pump_efficiency", current_state.pump_efficiency},
        {"pressure_gradient", current_state.pressure_gradient},
        {"last_update", current_state.last_update}
    };
}