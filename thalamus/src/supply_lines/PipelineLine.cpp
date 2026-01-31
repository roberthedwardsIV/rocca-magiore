#include "PipelineLine.hpp"
#include <algorithm>
#include <cmath>

// Constructor
PipelineLine::PipelineLine(int id, std::string name) 
    : BaseSupplyLine(id, name, "pipeline_line") {
    
    this->process_noise = 0.001f; 
    
    current_state = {
        1.0f, 0.0f,  // Integrity + Blockage
        1.0f,        // Target Flow 
        1000.0f,     // Base Pressure PSI
        1.0f,        // Effective Flow
        0.5f, 0.5f, 0.5f, // Uncertainties
        0LL
    };
}


// Packet Processor -> sends signal to apply_signal() with mutex locked
void PipelineLine::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting pipeline line state vector with Kalman logic
void PipelineLine::apply_signal(const json& sig) {
    current_state.unc_struct += process_noise;
    current_state.unc_block += process_noise;
    current_state.unc_flow += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "integrity") {
        float z = 1.0f - severity;
        float K = current_state.unc_struct / (current_state.unc_struct + R);
        current_state.structural_integrity += K * (z - current_state.structural_integrity);
        current_state.unc_struct *= (1.0f - K);
    } 
    else if (category == "blockage") {
        float K = current_state.unc_block / (current_state.unc_block + R);
        current_state.blockage_severity += K * (severity - current_state.blockage_severity);
        current_state.unc_block *= (1.0f - K);
    }
    else if (category == "flow_setpoint") {
        if (sig.contains("value")) {
            float z_flow = sig["value"].get<float>();
            float K = current_state.unc_flow / (current_state.unc_flow + R);
            current_state.target_flow_rate += K * (z_flow - current_state.target_flow_rate);
            current_state.unc_flow *= (1.0f - K);
        }
    }

    // Pressure + Flow
    float base_pressure = 1000.0f; // Nominal PSI
    
    float max_safe_pressure = base_pressure * current_state.structural_integrity;
    
    float target_pressure = base_pressure * current_state.target_flow_rate;
    
    current_state.current_pressure_psi = std::min(target_pressure, max_safe_pressure);

    float pressure_ratio = current_state.current_pressure_psi / base_pressure;
    float blockage_factor = 1.0f - current_state.blockage_severity;
    
    current_state.effective_flow_rate = pressure_ratio * blockage_factor;
    
    if (current_state.effective_flow_rate < 0.0f) current_state.effective_flow_rate = 0.0f;

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json PipelineLine::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"line_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"structural_integrity", current_state.structural_integrity},
        {"blockage_severity", current_state.blockage_severity},
        {"current_pressure_psi", current_state.current_pressure_psi},
        {"effective_flow_rate", current_state.effective_flow_rate},
        {"last_update", current_state.last_update}
    };
}