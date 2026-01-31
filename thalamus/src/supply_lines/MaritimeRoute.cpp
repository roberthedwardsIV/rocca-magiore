#include "MaritimeRoute.hpp"
#include <algorithm>
#include <cmath>

// Constructor
MaritimeRoute::MaritimeRoute(int id, std::string name) 
    : BaseSupplyLine(id, name, "maritime_route") {
    
    this->process_noise = 0.002f; 
    
    current_state = {
        1.0f, 0.1f, // Nav + Weather 
        0.0f,       // Security 
        22.0f,      // Speed 
        0.5f, 0.5f, 0.5f, // Uncertainties
        0LL
    };
}


// Packet Processor -> sends signal to apply_signal() with mutex locked
void MaritimeRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(supply_mutex);
    apply_signal(sig);
}


// Signal applier -> merges new data from signals to exisiting maritime route state vector with Kalman logic
void MaritimeRoute::apply_signal(const json& sig) {
    current_state.unc_nav += process_noise;
    current_state.unc_weather += process_noise;
    current_state.unc_sec += process_noise;

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
    else if (category == "weather") {
        float K = current_state.unc_weather / (current_state.unc_weather + R);
        current_state.weather_severity += K * (severity - current_state.weather_severity);
        current_state.unc_weather *= (1.0f - K);
    }
    else if (category == "security") {
        float K = current_state.unc_sec / (current_state.unc_sec + R);
        current_state.security_risk += K * (severity - current_state.security_risk);
        current_state.unc_sec *= (1.0f - K);
    }

    // Speed Calculation
    float base_speed = 22.0f; // Standard container ship cruising speed
    
    float weather_penalty = 1.0f;
    if (current_state.weather_severity > 0.4f) {
        weather_penalty = 1.0f - ((current_state.weather_severity - 0.4f) * 1.0f);
        if (weather_penalty < 0.2f) weather_penalty = 0.2f; 
    }

    float security_penalty = 1.0f;
    if (current_state.security_risk > 0.7f) {
        security_penalty = 0.5f; 
    }

    current_state.effective_speed_knots = base_speed * weather_penalty * security_penalty * current_state.navigability;

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json MaritimeRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(supply_mutex);
    return {
        {"route_id", line_id},
        {"name", name},
        {"entity_type", entity_type},
        {"navigability", current_state.navigability},
        {"weather_severity", current_state.weather_severity},
        {"security_risk", current_state.security_risk},
        {"effective_speed_knots", current_state.effective_speed_knots},
        {"last_update", current_state.last_update}
    };
}