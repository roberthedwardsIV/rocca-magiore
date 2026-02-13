#include "RailRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

// Constructor for initialization
RailRoute::RailRoute(long long id, std::string name)
    : BaseRoute(id, name, "rail") {
    
    // Default values at spinup
    gauge_mm = 1435.0f;        
    is_electrified = false;
    voltage_kv = 0.0f;
    max_axle_load_tons = 22.5f; 
    loading_gauge_height = 4.0f;
    number_of_tracks = 1;
    design_speed_kmh = 80.0f;   
    signaling_headway_min = 15.0f; 
    current_flow_trains_per_hour = 0.0f;
    track_integrity = 1.0f;     
    power_active = true;
    effective_max_speed_kmh = design_speed_kmh;
    travel_time_hours = 0.0f;
    overturning_wind_speed_kmh = 200.0f; // Safe default before first calc
    
    update_metrics();
}


// Direct updates to static fields from OSM/Others
void RailRoute::parse_osm_tags() {
    // Gauge
    if (tags.count("gauge")) {
        try { gauge_mm = std::stof(tags["gauge"]); } catch (...) { gauge_mm = 1435.0f; }
    }

    // Electrification
    if (tags.count("electrified")) {
        std::string e = tags["electrified"];
        if (e == "contact_line" || e == "rail" || e == "yes") {
            is_electrified = true;
            voltage_kv = 25.0f; 
        }
    }

    // Voltage
    if (tags.count("voltage")) {
        try { voltage_kv = std::stof(tags["voltage"]) / 1000.0f; } catch (...) {}
    }

    // Track Count
    if (tags.count("tracks")) {
        try { number_of_tracks = std::stoi(tags["tracks"]); } catch (...) { number_of_tracks = 1; }
    }

    // Design Speed
    if (tags.count("maxspeed")) {
        try { design_speed_kmh = std::stof(tags["maxspeed"]); } catch (...) { design_speed_kmh = 80.0f; }
    } else if (tags.count("usage")) {
        if (tags["usage"] == "main") design_speed_kmh = 100.0f;
        else design_speed_kmh = 40.0f; 
    }

    // Electrified lines allow higher stacks (+0.5 m)
    if (is_electrified) loading_gauge_height = 4.5f; 
}


// Updates to dynamic + calculated fields from Thalamus signaling
void RailRoute::update_metrics() {
    // ---- Overturning Wind Speed Calculation ----
    // Formula derived from moment balance: 
    // Resisting Moment (Gravity) = Mass * g * (Gauge/2)
    // Overturning Moment (Wind) = Pressure * Area * CenterOfPressure
    // V_crit = sqrt( (Mass * g * Gauge) / (Coeff * Height^2) )
    const float g = 9.81f;          // m/s2
    const float rho_air = 1.225f;   // kg/m3
    const float drag_coeff = 2.0f; 
    float mass_per_meter = (max_axle_load_tons * 4.0f * 1000.0f) / 15.0f;     // Mass per meter
    float gauge_m = gauge_mm / 1000.0f;
    float numerator = mass_per_meter * g * (gauge_m / 2.0f);
    float denominator = 0.5f * rho_air * drag_coeff * (loading_gauge_height / 2.0f) * loading_gauge_height;
    
    if (denominator > 0.001f) {
        float v_crit_ms = std::sqrt(numerator / denominator);
        overturning_wind_speed_kmh = v_crit_ms * 3.6f;
    } else {
        overturning_wind_speed_kmh = 999.0f; // Impossible to tip
    }

    // ---- Capacity ----
    // (Trains/Hr) = (60 mins / Headway) * Tracks * Efficiency
    float track_eff = (number_of_tracks == 1) ? 0.4f : 1.0f;
    float capacity_tph = (60.0f / signaling_headway_min) * number_of_tracks * track_eff;

    // ---- Latency (BPR Function)----
    // T = T_free * (1 + alpha * (Flow/Capacity)^beta)
    // T_free = Length / EffectiveSpeed
    if (get_length_km() > 0) {
        float t_free = get_length_km() / std::max(1.0f, effective_max_speed_kmh);
        travel_time_hours = calculate_congestion_delay(current_flow_trains_per_hour, capacity_tph) * t_free;
    }
}


// Helper BPR Function for latency calculations
float RailRoute::calculate_congestion_delay(float volume, float capacity) const {
    if (capacity <= 0.0f) return 9999.0f; // Blocked
    const float alpha = 0.15f;
    const float beta = 4.0f;
    float saturation = volume / capacity;
    return 1.0f + (alpha * std::pow(saturation, beta));
}


// Main signal routing function to properly apply update_metrics() to new signal arrival
void RailRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::string category = sig.value("category", "none");

    // Integrity + Maintenance (effective_max_speed + track_integrity)
    if (category == "integrity" || category == "maintenance") {
        float severity = sig.value("severity", 0.0f);
        track_integrity = std::max(0.0f, 1.0f - severity);
        
        if (track_integrity < 0.3f) {
            effective_max_speed_kmh = 0.0f;
        } else {
            effective_max_speed_kmh = design_speed_kmh * track_integrity;
        }
    }

    // Weather signals (effective_max_speed)
    else if (category == "weather") {
        float wind = sig.value("wind_speed_kmh", 0.0f);
        if (wind > overturning_wind_speed_kmh) {
            effective_max_speed_kmh = 0.0f; // STOP
        } else if (wind > (overturning_wind_speed_kmh * 0.7f)) {
            effective_max_speed_kmh = std::min(effective_max_speed_kmh, 40.0f); // RESTRICT
        }
    }

    // Power signals (effective_speed if electrified)
    else if (category == "power" && is_electrified) {
        power_active = sig.value("active", true);
        if (!power_active) {
            effective_max_speed_kmh = 0.0f; 
        }
    }

    // Traffic signals (current_flow)
    else if (category == "traffic") {
        current_flow_trains_per_hour = sig.value("flow_tph", 0.0f);
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics(); 
}


// JSON packager for archival
json RailRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(route_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"flow_tph", current_flow_trains_per_hour},
        {"travel_time_h", travel_time_hours},
        {"integrity", track_integrity},
        {"wind_limit_kmh", overturning_wind_speed_kmh},
        {"power_active", power_active},
        {"last_update", last_update}
    };
}