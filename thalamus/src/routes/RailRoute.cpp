#include "RailRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

RailRoute::RailRoute(long long id, std::string name)
    : BaseRoute(id, name, "rail") {
    
    // Default Engineering Constraints (Standard Gauge / Non-Electrified)
    gauge_mm = 1435.0f;        // Standard Gauge
    is_electrified = false;
    voltage_kv = 0.0f;
    max_axle_load_tons = 22.5f;// EU Standard (UIC)
    loading_gauge_height = 4.0f; // Meters
    
    // Default Capacity Metrics
    number_of_tracks = 1;      // Assume single track unless specified
    max_speed_kmh = 80.0f;     // Freight speed
    signaling_headway_min = 15.0f; // 15 mins between trains (Conservative)
    
    current_flow_trains_per_hour = 0.0f;
    is_main_line = true;
}

void RailRoute::parse_osm_tags() {
    // 1. Gauge (The "Break of Gauge" Problem)
    if (tags.count("gauge")) {
        try {
            // OSM usually provides mm (e.g., "1435", "1520")
            gauge_mm = std::stof(tags["gauge"]);
        } catch (...) { 
            // Fallback: Infer from region if we had location data, 
            // but for now default to standard.
            gauge_mm = 1435.0f; 
        }
    }

    // 2. Electrification & Voltage
    if (tags.count("electrified")) {
        std::string e = tags["electrified"];
        if (e == "contact_line" || e == "rail" || e == "yes") {
            is_electrified = true;
        } else {
            is_electrified = false;
        }
    }

    if (tags.count("voltage")) {
        try {
            // OSM values: "25000", "15000", "3000"
            float raw_volts = std::stof(tags["voltage"]);
            voltage_kv = raw_volts / 1000.0f; // Store as kV
        } catch (...) { voltage_kv = 0.0f; }
    }

    // 3. Track Count (The #1 Capacity Driver)
    if (tags.count("tracks")) {
        try {
            number_of_tracks = std::stoi(tags["tracks"]);
        } catch (...) { number_of_tracks = 1; }
    }

    // 4. Usage / Service
    if (tags.count("usage")) {
        std::string u = tags["usage"];
        if (u == "main" || u == "freight") is_main_line = true;
        else if (u == "branch" || u == "industrial" || u == "service") is_main_line = false;
    }
    
    // 5. Speed Limits
    if (tags.count("maxspeed")) {
        try {
            max_speed_kmh = std::stof(tags["maxspeed"]);
        } catch (...) { 
            max_speed_kmh = is_main_line ? 100.0f : 40.0f; 
        }
    }
}

void RailRoute::update_metrics() {
    // Recalculate physical length if needed
    if (length_km <= 0.001f) {
        length_km = get_length_km(); 
    }

    // --- Capacity Calculation (Trains Per Hour) ---
    // Theoretical Max = 60 mins / Headway * Tracks
    
    // Double Track (2) allows simultaneous bi-directional flow.
    // Single Track (1) requires passing loops, reducing capacity by ~60-70%.
    
    float track_factor = (float)number_of_tracks;
    
    if (number_of_tracks == 1) {
        // Single track efficiency is poor due to waiting for opposing traffic
        track_factor = 0.4f; 
    }
    
    // Signaling Technology Impact
    // If not specified, assume standard block signaling (10-15 min headway)
    // If "high_speed", assume ETCS Level 2 (3 min headway)
    if (max_speed_kmh > 160.0f) signaling_headway_min = 4.0f;
    else if (is_main_line) signaling_headway_min = 10.0f;
    else signaling_headway_min = 20.0f;

    float theoretical_capacity = (60.0f / signaling_headway_min) * track_factor;

    // --- Saturation Penalty ---
    // If current flow approaches theoretical capacity, delays explode exponentially.
    float saturation = 0.0f;
    if (theoretical_capacity > 0) saturation = current_flow_trains_per_hour / theoretical_capacity;

    float delay_factor = 1.0f;
    if (saturation > 0.9f) delay_factor = 3.0f; // Massive delays
    else if (saturation > 0.7f) delay_factor = 1.5f;

    // Final Travel Time
    // T = (Distance / Speed) * Delay
    travel_time_hours = (length_km / max_speed_kmh) * delay_factor;
}