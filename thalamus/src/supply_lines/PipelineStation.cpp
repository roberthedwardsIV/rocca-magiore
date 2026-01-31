#include "PipelineStation.hpp"
#include <algorithm>
#include <cmath>

// Constructor
PipelineStation::PipelineStation(int id, std::string name) 
    : BaseSupplyLine(id, name, "pipeline_station") {
    
    this->process_noise = 0.002f; 
    
    current_state = {
        1.0f, 1.0f,  
        1.0f, 0.8f,  
        1.0f,       
        0.5f, 0.5f, 0.5f, 0.5f, 
        0LL
    };
}


// Packet Processor -> sends signal to apply_signal() with mutex locked
void PipelineStation::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting pipeline station state vector with Kalman logic
void PipelineStation::apply_signal(const json& sig) {
    current_state.unc_mech += process_noise;
    current_state.unc_cool += process_noise;
    current_state.unc_pow += process_noise;
    current_state.unc_load += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "mechanical") {
        float z = 1.0f - severity;
        float K = current_state.unc_mech / (current_state.unc_mech + R);
        current_state.mechanical_health += K * (z - current_state.mechanical_health);
        current_state.unc_mech *= (1.0f - K);
    } 
    else if (category == "cooling") {
        float z = 1.0f - severity;
        float K = current_state.unc_cool / (current_state.unc_cool + R);
        current_state.cooling_system_health += K * (z - current_state.cooling_system_health);
        current_state.unc_cool *= (1.0f - K);
    }
    else if (category == "power") {
        float z = 1.0f - severity;
        float K = current_state.unc_pow / (current_state.unc_pow + R);
        current_state.power_availability += K * (z - current_state.power_availability);
        current_state.unc_pow *= (1.0f - K);
    }
    else if (category == "load_setpoint") {
        if (sig.contains("value")) {
            float z_load = sig["value"].get<float>();
            float K = current_state.unc_load / (current_state.unc_load + R);
            current_state.target_load += K * (z_load - current_state.target_load);
            current_state.unc_load *= (1.0f - K);
        }
    }

    // Output Efficiency
    
    float physical_cap = std::min(current_state.mechanical_health, current_state.power_availability);
    
    float thermal_factor = 1.0f;
    if (current_state.target_load > 0.7f && current_state.cooling_system_health < 0.8f) {
        float overheat_severity = (current_state.target_load - 0.7f) + (0.8f - current_state.cooling_system_health);
        thermal_factor = std::max(0.2f, 1.0f - overheat_severity);
    }

    current_state.output_efficiency = physical_cap * thermal_factor;

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json PipelineStation::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"station_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"mechanical_health", current_state.mechanical_health},
        {"cooling_system_health", current_state.cooling_system_health},
        {"power_availability", current_state.power_availability},
        {"output_efficiency", current_state.output_efficiency},
        {"last_update", current_state.last_update}
    };
}