#include "EarthquakeTracker.hpp"
#include <iostream>
#include <cmath>

// Constructor (sends to process_packet upon creation)
EarthquakeTracker::EarthquakeTracker(const json& initial_sig) {
    entity_id = initial_sig["entity_id"];
    entity_type = "earthquake";

    // Tracker initialized with high uncertainty (reduced as reliable signals pour in)
    current_state.uncertainty = 1.0f; 
    current_state.process_noise = 0.005f; 
    current_state.event_time = initial_sig["timestamp"]; 
    current_state.lat = 0.0f;
    current_state.lon = 0.0f;
    current_state.magnitude = 0.0f;
    current_state.intensity = 0.0f;

    process_packet(initial_sig);
}


// Applies signal using one-dimensional Kalman Filter
void EarthquakeTracker::apply_signal_to_state(const RawPacket& pkt) {
    float P_pred = current_state.uncertainty + current_state.process_noise;

    float R = pkt.reliability; 

    // K = factor by which new data affects current state stats (Kalman gain)
    float K = P_pred / (P_pred + R); 
    
    if (pkt.data.contains("mag") && !pkt.data["mag"].is_null()) {
        float z_mag = pkt.data["mag"];
        current_state.magnitude += K * (z_mag - current_state.magnitude);
    }

    if (pkt.data.contains("mmi") && !pkt.data["mmi"].is_null()) {
        float z_mmi = pkt.data["mmi"];
        current_state.intensity += K * (z_mmi - current_state.intensity);
    }

    if (pkt.data.contains("lat") && pkt.data.contains("lon")) {
        float z_lat = pkt.data["lat"];
        float z_lon = pkt.data["lon"];
        current_state.lat += K * (z_lat - current_state.lat);
        current_state.lon += K * (z_lon - current_state.lon);
    }

    current_state.uncertainty = (1.0f - K) * P_pred; // Uncertainty gets updated each new merge
    current_state.update_time = pkt.timestamp;
}


// Reset state to start conditions before replaying entire history when receiving late data
void EarthquakeTracker::full_recalculate() {
    current_state.magnitude = 0;
    current_state.intensity = 0;
    current_state.uncertainty = 1.0f;

    current_state.lat = history_log.front().data.value("lat", 0.0f);
    current_state.lon = history_log.front().data.value("lon", 0.0f);

    for (const auto& pkt : history_log) {
        apply_signal_to_state(pkt);
    }
}


// Packet processor (main control method sending data to apply_signal_to_state)
void EarthquakeTracker::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(state_mutex); 

    RawPacket pkt;
    pkt.timestamp = sig["timestamp"];
    pkt.data = sig["data"];
    pkt.reliability = sig.value("reliability_noise", 0.5f);

    // Check if this signal is arriving "out of order" (earlier than our last update) + recalculate if so
    bool is_late = (!history_log.empty() && pkt.timestamp < history_log.back().timestamp);
    history_log.push_back(pkt);
    if (is_late) {
        std::sort(history_log.begin(), history_log.end(), 
                  [](const RawPacket& a, const RawPacket& b) { return a.timestamp < b.timestamp; });
        full_recalculate();
    } else {
        apply_signal_to_state(pkt);
    }

    current_state.zr_score = calculate_zr_score();
}


// Calculates earthquake zr_score by normalizing intensity, magnitude and applying confidence factor (0-1)
float EarthquakeTracker::calculate_zr_score() {
    float norm_mmi = current_state.intensity / 12.0f;
    float norm_mag = current_state.magnitude / 10.0f;
    float severity = (norm_mmi + 0.01f) * (norm_mag + 0.01f);
    float confidence = 1.0f - std::min(1.0f, current_state.uncertainty);
    
    return severity * confidence;
}


// Controller for calculating staleness of signals (to archive to db after 1 hour of silence)
bool EarthquakeTracker::is_stale(long long current_time) const {
    const long long ONE_HOUR_MS = 3600000;
    return (current_time - current_state.update_time) > ONE_HOUR_MS;
}


// Pull last update time
long long EarthquakeTracker::get_last_update_time() const {
    return current_state.update_time;
}


// Function to package state vectors and full history into json for DB archiving
json EarthquakeTracker::to_json() const {
    json j;
    j["entity_id"] = entity_id;
    j["entity_type"] = entity_type;
    j["final_mag"] = current_state.magnitude;
    j["final_intensity"] = current_state.intensity;
    j["uncertainty"] = current_state.uncertainty;
    j["zr_score"] = current_state.zr_score;
    j["lat"] = current_state.lat;
    j["lon"] = current_state.lon;
    j["start_time"] = current_state.event_time;
    j["end_time"] = current_state.update_time;
    
    j["history_log"] = json::array();
    for (const auto& pkt : history_log) {
        json p;
        p["ts"] = pkt.timestamp;
        p["reliability"] = pkt.reliability;
        
        json data_obj = json::object();
        if (pkt.data.is_object()) {
            data_obj["mag"] = pkt.data.value("mag", 0.0f);
            data_obj["mmi"] = pkt.data.value("mmi", 0.0f);
            data_obj["lat"] = pkt.data.value("lat", 0.0f);
            data_obj["lon"] = pkt.data.value("lon", 0.0f);
        }
        p["data"] = data_obj;
        j["history_log"].push_back(p);
    }
    
    return j;
}