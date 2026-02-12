#include "RailNodeHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

RailNodeHub::RailNodeHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "rail_node", lat, lon) {
    
    // Default Infrastructure (Small Siding)
    number_of_tracks = 2;
    max_train_length_m = 500.0f; // Standard freight train length
    is_hump_yard = false;        // Rare, high-tech infrastructure
    is_electrified = false;
    has_intermodal_lift = false;

    // Default Capacity
    classification_bowl_capacity = 100; // Wagons
    current_wagons_stored = 0;
    processing_speed_wagons_hr = 10.0f; // Flat switching is slow

    // Default Intermodal
    container_lifts_per_hour = 0;
    truck_gate_lanes = 0;

    // Default State
    congestion_level = 0.0f;
    average_dwell_time_h = 24.0f; // 1 day standard dwell
    
    yard_type = "siding";
    operator_name = "Unknown";
}

void RailNodeHub::parse_osm_tags() {
    // 1. Yard Type & Function
    if (tags.count("railway")) {
        std::string r = tags["railway"];
        if (r == "yard" || r == "marshalling_yard") {
            yard_type = "marshalling";
            number_of_tracks = 10; // Baseline for a yard
            max_train_length_m = 750.0f; // UIC standard
        } else if (r == "station") {
            yard_type = "station";
            // Passenger focus, but often has freight pass-through
        } else if (r == "intermodal_terminal" || r == "container_terminal") {
            yard_type = "intermodal";
            has_intermodal_lift = true;
            container_lifts_per_hour = 30; // 1 crane
            truck_gate_lanes = 2;
        }
    }

    // 2. Electrification (Critical for Locomotives)
    if (tags.count("electrified")) {
        std::string e = tags["electrified"];
        if (e == "yes" || e == "contact_line" || e == "rail") {
            is_electrified = true;
        }
    }

    // 3. Track Count (The #1 Capacity Metric)
    // Often inferred from geometry or specific tags like "tracks"
    if (tags.count("tracks")) {
        try { number_of_tracks = std::stoi(tags["tracks"]); } catch (...) {}
    } else if (tags.count("service") && tags["service"] == "siding") {
        number_of_tracks = 1; // Just a siding
        yard_type = "siding";
    }

    // 4. Hump Yard Detection (Heuristic)
    // Hump yards are massive. If tracks > 20, likely a hump yard or major terminal.
    if (number_of_tracks > 20) {
        is_hump_yard = true;
        processing_speed_wagons_hr = 150.0f; // Gravity sorting is fast!
        classification_bowl_capacity = number_of_tracks * 50; // 50 wagons per track
    } else {
        // Flat Yard (Shunting)
        is_hump_yard = false;
        processing_speed_wagons_hr = 20.0f; // Slow switch engine work
        classification_bowl_capacity = number_of_tracks * 40;
    }

    // 5. Intermodal Specifics
    if (tags.count("cargo") && tags["cargo"] == "yes") {
        has_intermodal_lift = true;
        // Boost capacity if explicitly cargo
        container_lifts_per_hour = 50; 
    }
}

void RailNodeHub::update_status() {
    // 1. Calculate Congestion
    if (classification_bowl_capacity > 0) {
        congestion_level = (float)current_wagons_stored / (float)classification_bowl_capacity;
    } else {
        congestion_level = 0.0f;
    }

    // 2. Dwell Time Penalty (The Sliding Puzzle Problem)
    // As yard fills > 80%, sorting speed plummets because there's no room to shuffle cars.
    
    float sort_efficiency = 1.0f;
    
    if (congestion_level > 0.95f) {
        sort_efficiency = 0.1f; // Gridlock
        average_dwell_time_h = 72.0f; // 3 days stuck
    } else if (congestion_level > 0.80f) {
        sort_efficiency = 0.5f; // Heavy delays
        average_dwell_time_h = 48.0f;
    } else {
        average_dwell_time_h = 24.0f; // Nominal
    }

    // 3. Adjust Processing Speed
    // Effective throughput = Base Speed * Efficiency
    // e.g., A Hump Yard (150 cars/hr) running at 95% capacity might drop to 15 cars/hr.
    float effective_processing = processing_speed_wagons_hr * sort_efficiency;

    // If intermodal, update lift capacity too
    if (has_intermodal_lift) {
        // Truck gate delays if yard is full
        if (congestion_level > 0.9f) container_lifts_per_hour = 5; // Gate backup
    }
}

bool RailNodeHub::can_accept_train(float train_length_m) const {
    // 1. Length Check
    if (train_length_m > max_train_length_m) return false;

    // 2. Capacity Check
    // If we are gridlocked, refuse entry (train must hold on the main line)
    if (congestion_level >= 1.0f) return false;

    return true;
}