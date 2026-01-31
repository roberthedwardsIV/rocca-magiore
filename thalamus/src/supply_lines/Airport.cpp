#include "Airport.hpp"
#include <algorithm>
#include <cmath>

// Constructor
Airport::Airport(int id, std::string name) 
    : BaseSupplyLine(id, name, "airport") {
    
    this->process_noise = 0.003f; 
    
    current_state = {
        1.0f, 0.8f, 
        1.0f, 1.0f, 
        0.5f,
        0.5f, 0.5f, 0.5f, 0.5f, 
        0LL
    };
}


// Packet processor -> sends to apply_signal() with mutex locked
void Airport::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting airport state vector with Kalman logic
void Airport::apply_signal(const json& sig) {
    current_state.unc_run += process_noise;
    current_state.unc_cargo += process_noise;
    current_state.unc_atc += process_noise;
    current_state.unc_fuel += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    // Runway updates
    if (category == "runway") {
        float z = 1.0f - severity;
        float K = current_state.unc_run / (current_state.unc_run + R);
        current_state.runway_integrity += K * (z - current_state.runway_integrity);
        current_state.unc_run *= (1.0f - K);
    } 
    // ATC Efficiency updates
    else if (category == "atc") {
        float z = 1.0f - severity;
        float K = current_state.unc_atc / (current_state.unc_atc + R);
        current_state.atc_efficiency += K * (z - current_state.atc_efficiency);
        current_state.unc_atc *= (1.0f - K);
    }
    // Fuel Availability Updates
    else if (category == "logistics") {
        float z = 1.0f - severity;
        float K = current_state.unc_fuel / (current_state.unc_fuel + R);
        current_state.fuel_availability += K * (z - current_state.fuel_availability);
        current_state.unc_fuel *= (1.0f - K);
    }
    // Processing Rate Updates
    else if (category == "throughput") {
        if (sig.contains("value")) {
            float z_cargo = sig["value"].get<float>();
            float K = current_state.unc_cargo / (current_state.unc_cargo + R);
            current_state.cargo_throughput += K * (z_cargo - current_state.cargo_throughput);
            current_state.unc_cargo *= (1.0f - K);
        }
    }

    // Delay Time Update logic
    float base_delay = 0.5f; // 30 mins standard
    
    // Runway/ATC Constraint
    float operational_capacity = current_state.runway_integrity * current_state.atc_efficiency;
    
    if (operational_capacity < current_state.cargo_throughput) {
        float congestion = current_state.cargo_throughput - operational_capacity;
        // Exponential delay penalty for congestion
        current_state.processing_delay_hours = base_delay + (std::exp(congestion * 5.0f) - 1.0f);
    } else {
        current_state.processing_delay_hours = base_delay;
    }

    // Fuel Shortage Constraint
    if (current_state.fuel_availability < 0.3f) {
        current_state.processing_delay_hours += 4.0f; // Add 4 hours for refueling waits
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json Airport::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"airport_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"runway_integrity", current_state.runway_integrity},
        {"cargo_throughput", current_state.cargo_throughput},
        {"atc_efficiency", current_state.atc_efficiency},
        {"fuel_availability", current_state.fuel_availability},
        {"processing_delay_hours", current_state.processing_delay_hours},
        {"last_update", current_state.last_update}
    };
}