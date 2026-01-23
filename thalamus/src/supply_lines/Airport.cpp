#include "Airport.hpp"
#include <algorithm>

Airport::Airport(int id, std::string name) {
    this->line_id = id;
    this->name = name;
    this->line_type = "airport";
    this->process_noise = 0.003f; 
    current_state = {1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0LL};
}

void Airport::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(line_mutex);
    apply_signal(sig);
}

void Airport::apply_signal(const json& sig) {
    current_state.unc_run += process_noise;
    current_state.unc_atc += process_noise;
    current_state.unc_fuel += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "runway") {
        float z = 1.0f - severity;
        float K = current_state.unc_run / (current_state.unc_run + R);
        current_state.runway_integrity += K * (z - current_state.runway_integrity);
        current_state.unc_run *= (1.0f - K);
    } 
    else if (category == "atc") {
        float z = 1.0f - severity;
        float K = current_state.unc_atc / (current_state.unc_atc + R);
        current_state.atc_capacity += K * (z - current_state.atc_capacity);
        current_state.unc_atc *= (1.0f - K);
    }
    else if (category == "logistics") {
        float z = 1.0f - severity;
        float K = current_state.unc_fuel / (current_state.unc_fuel + R);
        current_state.fuel_availability += K * (z - current_state.fuel_availability);
        current_state.unc_fuel *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json Airport::get_json_state() const {
    std::lock_guard<std::mutex> lock(line_mutex);
    return {
        {"airport_id", line_id},
        {"name", name},
        {"runway_integrity", current_state.runway_integrity},
        {"atc_capacity", current_state.atc_capacity},
        {"fuel_availability", current_state.fuel_availability},
        {"last_update", current_state.last_update}
    };
}