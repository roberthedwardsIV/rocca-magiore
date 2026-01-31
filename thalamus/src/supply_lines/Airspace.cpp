#include "Airspace.hpp"
#include <algorithm>
#include <cmath>

// Constructor
Airspace::Airspace(int id, std::string name) 
    : BaseSupplyLine(id, name, "airspace") {
    
    this->process_noise = 0.01f; 
    
    current_state = {
        1.0f, 0.5f,  
        0.0f,        
        1.0f,        
        0.5f, 0.5f, 0.5f, 
        0LL
    };
}


// Packet processor -> sends to apply_signal() with mutex locked
void Airspace::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting airspace state vector with Kalman logic
void Airspace::apply_signal(const json& sig) {
    current_state.unc_nav += process_noise;
    current_state.unc_vol += process_noise;
    current_state.unc_risk += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "navigability") {
        float z = 1.0f - severity;
        float K = current_state.unc_nav / (current_state.unc_nav + R);
        current_state.navigability += K * (z - current_state.navigability);
        current_state.unc_nav *= (1.0f - K);
    } 
    else if (category == "risk") {
        float K = current_state.unc_risk / (current_state.unc_risk + R);
        current_state.risk_index += K * (severity - current_state.risk_index);
        current_state.unc_risk *= (1.0f - K);

        // High risk forces closure
        if (current_state.risk_index > 0.6f) {
            float safety_cap = 1.0f - current_state.risk_index;
            if (current_state.navigability > safety_cap) {
                current_state.navigability = safety_cap;
            }
        }
    }
    else if (category == "volume") {
        if (sig.contains("value")) {
            float z_vol = sig["value"].get<float>();
            float K = current_state.unc_vol / (current_state.unc_vol + R);
            current_state.traffic_volume += K * (z_vol - current_state.traffic_volume);
            current_state.unc_vol *= (1.0f - K);
        }
    }

    // If volume exceeds available navigability, congestion spikes non-linearly
    if (current_state.traffic_volume > current_state.navigability) {
        float overflow = current_state.traffic_volume - current_state.navigability;
        current_state.congestion_penalty = 1.0f + std::exp(overflow * 3.0f);
    } else {
        current_state.congestion_penalty = 1.0f;
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json Airspace::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"airspace_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"navigability", current_state.navigability},
        {"traffic_volume", current_state.traffic_volume},
        {"risk_index", current_state.risk_index},
        {"congestion_penalty", current_state.congestion_penalty},
        {"last_update", current_state.last_update}
    };
}