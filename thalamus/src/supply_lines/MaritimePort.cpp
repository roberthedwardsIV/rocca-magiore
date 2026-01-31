#include "MaritimePort.hpp"
#include <algorithm>
#include <cmath>

// Constructor
MaritimePort::MaritimePort(int id, std::string name) 
    : BaseSupplyLine(id, name, "maritime_port") {
    
    this->process_noise = 0.003f; 
    
    current_state = {
        1.0f, 1.0f,  // Berth, Crane
        0.5f,        // Yard 
        1.0f,        // Throughput
        2.0f,        // Delay 
        0.5f, 0.5f, 0.5f, // Uncertainties
        0LL
    };
}


// Packet Processor -> sends signal to apply_signal() with mutex locked
void MaritimePort::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting maritime port state vector with Kalman logic
void MaritimePort::apply_signal(const json& sig) {
    current_state.unc_berth += process_noise;
    current_state.unc_crane += process_noise;
    current_state.unc_yard += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "berth") {
        float z = 1.0f - severity;
        float K = current_state.unc_berth / (current_state.unc_berth + R);
        current_state.berth_availability += K * (z - current_state.berth_availability);
        current_state.unc_berth *= (1.0f - K);
    } 
    else if (category == "crane") {
        float z = 1.0f - severity; 
        float K = current_state.unc_crane / (current_state.unc_crane + R);
        current_state.crane_health += K * (z - current_state.crane_health);
        current_state.unc_crane *= (1.0f - K);
    }
    else if (category == "yard") {
        if (sig.contains("value")) {
            float z_yard = sig["value"].get<float>();
            float K = current_state.unc_yard / (current_state.unc_yard + R);
            current_state.yard_utilization += K * (z_yard - current_state.yard_utilization);
            current_state.unc_yard *= (1.0f - K);
        }
    }

    // Throughput & Delays
    float space_available = 1.0f - current_state.yard_utilization;
    float infrastructure_cap = std::min(current_state.berth_availability, current_state.crane_health);
    
    current_state.effective_throughput = std::min(infrastructure_cap, space_available);

    // Delay Calculation
    float base_dwell = 2.0f; // Days
    
    // Gridlock Penalty
    if (current_state.yard_utilization > 0.9f) {
        float overflow = (current_state.yard_utilization - 0.9f) * 10.0f; // 0.0 to 1.0
        current_state.processing_delay_days = base_dwell + std::exp(overflow * 3.0f);
    } 
    // Mechanical Penalty
    else if (current_state.crane_health < 0.5f) {
        current_state.processing_delay_days = base_dwell + (5.0f * (1.0f - current_state.crane_health));
    }
    else {
        current_state.processing_delay_days = base_dwell;
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json MaritimePort::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"port_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"berth_availability", current_state.berth_availability},
        {"crane_health", current_state.crane_health},
        {"yard_utilization", current_state.yard_utilization},
        {"effective_throughput", current_state.effective_throughput},
        {"processing_delay_days", current_state.processing_delay_days},
        {"last_update", current_state.last_update}
    };
}