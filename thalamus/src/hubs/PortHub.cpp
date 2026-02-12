#include "PortHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

PortHub::PortHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "port", lat, lon) {
    
    // Default Physical Infrastructure (Small Feeder Port)
    number_of_berths = 2;
    max_draft_meters = 10.0f;   // Shallow
    total_quay_length_m = 400.0f;
    has_rail_connection = false;
    has_ro_ro_ramp = false;

    // Default Storage (Small Yard)
    max_storage_teu = 5000;
    current_storage_teu = 0;
    reefer_plugs = 50;

    // Default Operational Metrics
    gate_throughput_vph = 50.0f; // Trucks per hour
    average_dwell_time_h = 48.0f; // 2 days standard
    congestion_level = 0.0f;

    port_type = "general";
}

void PortHub::parse_osm_tags() {
    // 1. Port Type / Category
    if (tags.count("harbour:category")) {
        std::string cat = tags["harbour:category"];
        if (cat == "seaport") {
            port_type = "container"; // Assume major commercial
            max_draft_meters = 14.0f;
            number_of_berths = 6;
            max_storage_teu = 100000; 
        } else if (cat == "fishing") {
            port_type = "fishing";
            max_draft_meters = 5.0f;
            max_storage_teu = 100;
        } else if (cat == "ferry") {
            port_type = "ro_ro";
            has_ro_ro_ramp = true;
        }
    }

    // 2. Draft (Critical for Vessel Class)
    if (tags.count("mooring:depth")) {
        try { max_draft_meters = std::stof(tags["mooring:depth"]); } catch (...) {}
    } else if (tags.count("depth")) {
        try { max_draft_meters = std::stof(tags["depth"]); } catch (...) {}
    }

    // 3. Quay Length (Estimates Berths)
    if (tags.count("length")) {
        try {
            total_quay_length_m = std::stof(tags["length"]);
            // Rough rule: 1 berth = 300m for big ships, 150m for feeders
            if (max_draft_meters > 12.0f) number_of_berths = (int)(total_quay_length_m / 300.0f);
            else number_of_berths = (int)(total_quay_length_m / 150.0f);
            
            if (number_of_berths < 1) number_of_berths = 1;
        } catch (...) {}
    }

    // 4. Rail Connection
    if (tags.count("railway")) {
        has_rail_connection = true;
    }

    // 5. Explicit Capacity (Rare but gold standard)
    if (tags.count("capacity:teu")) {
        try { max_storage_teu = std::stoi(tags["capacity:teu"]); } catch (...) {}
    }

    // 6. Adjust Gate Capacity based on Size
    // If it's a mega-port (1M+ TEU), it needs massive gates
    if (max_storage_teu > 500000) gate_throughput_vph = 500.0f;
    else if (max_storage_teu > 50000) gate_throughput_vph = 150.0f;
}

void PortHub::update_status() {
    // 1. Calculate Congestion (Storage Utilization)
    if (max_storage_teu > 0) {
        congestion_level = (float)current_storage_teu / (float)max_storage_teu;
    } else {
        congestion_level = 0.0f;
    }

    // 2. Dwell Time Penalty (The "Gridlock" Effect)
    // If yard is >80% full, shuffle moves skyrocket, slowing everything down.
    float efficiency_factor = 1.0f;
    
    if (congestion_level > 0.95f) {
        efficiency_factor = 0.1f; // Total paralysis
        average_dwell_time_h = 240.0f; // 10 days (Crisis)
    } else if (congestion_level > 0.85f) {
        efficiency_factor = 0.5f; // Severe delays
        average_dwell_time_h = 120.0f; // 5 days
    } else if (congestion_level > 0.70f) {
        efficiency_factor = 0.8f; // Slow
        average_dwell_time_h = 72.0f; // 3 days
    } else {
        average_dwell_time_h = 48.0f; // Normal
    }

    // 3. Update Attached ChokePoints (Cranes)
    // If the yard is full, cranes must stop unloading (No place to put boxes).
    bool yard_is_full = (congestion_level > 0.98f);
    
    for (auto& cp : facilities) {
        // If we implemented a "set_operational_limit" on ChokePoints, we'd call it here.
        // For now, we assume the simulation engine reads the PortHub state 
        // and throttles the cranes externally.
    }
}

std::string PortHub::get_max_vessel_class() const {
    if (max_draft_meters >= 16.0f) return "Ultra Large Container Vessel (ULCV)";
    if (max_draft_meters >= 15.0f) return "New Panamax";
    if (max_draft_meters >= 12.0f) return "Panamax";
    if (max_draft_meters >= 10.0f) return "Feedermax";
    return "Coastal Feeder";
}