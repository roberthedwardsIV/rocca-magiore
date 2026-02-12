#include "WildfireTracker.hpp"
#include <cmath>
#include <iostream>

WildfireTracker::WildfireTracker(const json& initial_sig) {
    entity_id = initial_sig["entity_id"];
    entity_type = "wildfire";
    
    current_state.start_time = initial_sig["timestamp"];
    current_state.max_frp = 0.0f;
    current_state.containment_index = 0.0f;
    
    process_packet(initial_sig);
}

void WildfireTracker::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(data_mutex);
    
    // Extract data from the Python Brain's signal
    float new_frp = sig["data"].value("frp", 0.0f);
    float dist = sig["data"].value("dist_km", 999.0f);
    
    current_state.lat = sig["data"].value("lat", 0.0f);
    current_state.lon = sig["data"].value("lon", 0.0f);
    current_state.dist_km = dist;
    current_state.current_frp = new_frp;
    current_state.last_update = sig["timestamp"];

    // Update Peak Intensity
    if (new_frp > current_state.max_frp) {
        current_state.max_frp = new_frp;
        // If fire is growing (new peak), containment drops to 0
        current_state.containment_index = 0.0f;
    } else {
        // If intensity is dropping significantly from peak, assume some containment/die-off
        if (new_frp < (current_state.max_frp * 0.5f)) {
            current_state.containment_index = 0.5f;
        }
    }
}

float WildfireTracker::calculate_zr_score() {
    // 1. Intensity Score (FRP)
    // 100 MW is a very large fire. 10 MW is small.
    float intensity_score = current_state.current_frp / 100.0f;
    if (intensity_score > 1.0f) intensity_score = 1.0f;

    // 2. Proximity Score (Inverse Distance)
    // Critical at < 5km. Concern at < 50km.
    float proximity_score = 0.0f;
    if (current_state.dist_km < 1.0f) proximity_score = 1.0f;
    else proximity_score = 5.0f / current_state.dist_km; // e.g. at 10km, score is 0.5
    if (proximity_score > 1.0f) proximity_score = 1.0f;

    // Combined Risk
    float risk = (intensity_score * 0.4f) + (proximity_score * 0.6f);
    
    // Reduce risk if contained
    return risk * (1.0f - current_state.containment_index);
}

bool WildfireTracker::is_stale(long long current_time) const {
    // 12 Hours Stale Time (Satellites pass twice a day roughly)
    return (current_time - current_state.last_update) > 43200000;
}

json WildfireTracker::to_json() const {
    json j;
    j["entity_id"] = entity_id;
    j["entity_type"] = entity_type;
    j["lat"] = current_state.lat;
    j["lon"] = current_state.lon;
    j["current_frp"] = current_state.current_frp;
    j["max_frp"] = current_state.max_frp;
    j["dist_km"] = current_state.dist_km;
    j["zr_score"] = const_cast<WildfireTracker*>(this)->calculate_zr_score(); // Hack for const correctness
    return j;
}