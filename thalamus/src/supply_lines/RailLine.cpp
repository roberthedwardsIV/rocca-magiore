#include "RailLine.hpp"
#include <algorithm>
#include <cmath>

// Constructor
RailLine::RailLine(int id, std::string name) 
    : BaseSupplyLine(id, name, "rail_line") {
    
    this->process_noise = 0.003f; 
    
    specs.design_speed_kmh = 100.0f;       
    specs.critical_damage_threshold = 0.3f;
    specs.signal_fail_speed_factor = 0.25f;
    specs.congestion_soft_cap = 0.7f;     
    specs.congestion_scaling_factor = 2.5f;
    
    current_state = {
        1.0f, 1.0f, 1.0f, 
        0.4f,             
        specs.design_speed_kmh,
        0.5f, 0.5f, 0.5f, 0.5f, 
        0LL
    };
}


// Packet Processor -> sends signal to apply_signal() with mutex locked
void RailLine::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting rail line state vector with Kalman logic
void RailLine::apply_signal(const json& sig) {
    current_state.unc_track += process_noise;
    current_state.unc_elec += process_noise;
    current_state.unc_sig += process_noise;
    current_state.unc_cong += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "integrity") {
        float z = 1.0f - severity;
        float K = current_state.unc_track / (current_state.unc_track + R);
        current_state.track_integrity += K * (z - current_state.track_integrity);
        current_state.unc_track *= (1.0f - K);
    } 
    else if (category == "electrification") {
        float z = 1.0f - severity;
        float K = current_state.unc_elec / (current_state.unc_elec + R);
        current_state.electrification_status += K * (z - current_state.electrification_status);
        current_state.unc_elec *= (1.0f - K);
    }
    else if (category == "signal") {
        float z = 1.0f - severity;
        float K = current_state.unc_sig / (current_state.unc_sig + R);
        current_state.signal_health += K * (z - current_state.signal_health);
        current_state.unc_sig *= (1.0f - K);
    }
    else if (category == "congestion") {
        if (sig.contains("value")) {
            float z_cong = sig["value"].get<float>();
            float K = current_state.unc_cong / (current_state.unc_cong + R);
            current_state.congestion_level += K * (z_cong - current_state.congestion_level);
            current_state.unc_cong *= (1.0f - K);
        }
    }

    // Speed Limit Calculation
    
    float track_limit_factor = current_state.track_integrity;
    if (current_state.track_integrity < specs.critical_damage_threshold) {
        track_limit_factor = 0.0f; // Track unsafe
    }
    
    float signal_limit_factor = 1.0f;
    if (current_state.signal_health < 0.5f) {
        signal_limit_factor = specs.signal_fail_speed_factor;
    }

    float power_limit_factor = (current_state.electrification_status > 0.5f) ? 1.0f : 0.0f;

    float congestion_friction = 1.0f;
    if (current_state.congestion_level > specs.congestion_soft_cap) {
        float overflow = current_state.congestion_level - specs.congestion_soft_cap;
        congestion_friction = 1.0f - (overflow * specs.congestion_scaling_factor);
        
        if (congestion_friction < 0.1f) congestion_friction = 0.1f;
    }

    // Combined Speed
    current_state.max_safe_speed_kmh = specs.design_speed_kmh * track_limit_factor * signal_limit_factor * power_limit_factor * congestion_friction;

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json RailLine::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"line_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"track_integrity", current_state.track_integrity},
        {"electrification_status", current_state.electrification_status},
        {"signal_health", current_state.signal_health},
        {"congestion_level", current_state.congestion_level},
        {"max_safe_speed_kmh", current_state.max_safe_speed_kmh},
        {"design_speed_kmh", specs.design_speed_kmh}, 
        {"last_update", current_state.last_update}
    };
}