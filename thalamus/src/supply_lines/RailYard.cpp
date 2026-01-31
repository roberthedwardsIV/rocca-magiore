#include "RailYard.hpp"
#include <algorithm>
#include <cmath>

// Constructor
RailYard::RailYard(int id, std::string name) 
    : BaseSupplyLine(id, name, "rail_yard") {
    
    this->process_noise = 0.004f; 
    
    specs.base_dwell_time_hours = 24.0f;   
    specs.jam_threshold = 0.85f;            
    specs.gridlock_penalty_factor = 3.0f;   
    specs.critical_failure_threshold = 0.2f;
    
    current_state = {
        1.0f, 1.0f,  
        0.6f,        
        specs.base_dwell_time_hours,
        0.5f, 0.5f, 0.5f, 
        0LL
    };
}


// Packet Processor -> sends signal to apply_signal() with mutex locked
void RailYard::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting rail yard state vector with Kalman logic
void RailYard::apply_signal(const json& sig) {
    current_state.unc_sw += process_noise;
    current_state.unc_lab += process_noise;
    current_state.unc_occ += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "mechanical") {
        float z = 1.0f - severity;
        float K = current_state.unc_sw / (current_state.unc_sw + R);
        current_state.switch_health += K * (z - current_state.switch_health);
        current_state.unc_sw *= (1.0f - K);
    } 
    else if (category == "labor") {
        float z = 1.0f - severity; 
        float K = current_state.unc_lab / (current_state.unc_lab + R);
        current_state.labor_availability += K * (z - current_state.labor_availability);
        current_state.unc_lab *= (1.0f - K);
    }
    else if (category == "occupancy") {
        if (sig.contains("value")) {
            float z_occ = sig["value"].get<float>();
            float K = current_state.unc_occ / (current_state.unc_occ + R);
            current_state.yard_occupancy += K * (z_occ - current_state.yard_occupancy);
            current_state.unc_occ *= (1.0f - K);
        }
    }

    // Dwell Time Calculation
    
    float operational_capacity = std::min(current_state.switch_health, current_state.labor_availability);
    
    if (operational_capacity < specs.critical_failure_threshold) {
        current_state.current_dwell_time_hours = 999.0f; 
    } else {
        float congestion_factor = 1.0f;
        if (current_state.yard_occupancy > specs.jam_threshold) {
            float overflow = (current_state.yard_occupancy - specs.jam_threshold) / (1.0f - specs.jam_threshold);
            congestion_factor = 1.0f + (overflow * specs.gridlock_penalty_factor);
        }

       
        float efficiency_multiplier = 1.0f / (operational_capacity + 0.01f); // Avoid div/0

        current_state.current_dwell_time_hours = specs.base_dwell_time_hours * efficiency_multiplier * congestion_factor;
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json RailYard::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"yard_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"switch_health", current_state.switch_health},
        {"labor_availability", current_state.labor_availability},
        {"yard_occupancy", current_state.yard_occupancy},
        {"current_dwell_time_hours", current_state.current_dwell_time_hours},
        // Spec export
        {"base_dwell_time_hours", specs.base_dwell_time_hours},
        {"last_update", current_state.last_update}
    };
}