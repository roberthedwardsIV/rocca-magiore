#include "PowerSubstation.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>

PowerSubstation::PowerSubstation(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "substation", lat, lon) {
    
    // Default Electrical Infrastructure (Small Distribution)
    high_voltage_kv = 110.0f;   // Standard feed
    low_voltage_kv = 20.0f;     // Standard distribution
    transformer_count = 1;      // Single point of failure
    
    // Default Capacity
    total_capacity_mva = 40.0f; // Typical for a small city sub
    current_load_mw = 0.0f;
    power_factor = 0.95f;       // Good grid discipline

    // Default State
    is_tripped = false;
    transformer_temp_c = 45.0f; // Operating temp
    
    substation_type = "distribution";
}

void PowerSubstation::parse_osm_tags() {
    // 1. Voltage Parsing (Crucial for Capacity)
    // OSM often lists multiple voltages: "voltage=380000;110000"
    if (tags.count("voltage")) {
        std::string v_str = tags["voltage"];
        std::replace(v_str.begin(), v_str.end(), ';', ' ');
        std::stringstream ss(v_str);
        float v;
        std::vector<float> voltages;
        while (ss >> v) {
            // Normalize massive numbers to kV
            if (v > 1000.0f) v /= 1000.0f;
            voltages.push_back(v);
        }
        
        if (!voltages.empty()) {
            std::sort(voltages.begin(), voltages.end());
            low_voltage_kv = voltages.front(); // Lowest
            high_voltage_kv = voltages.back(); // Highest
        }
    }

    // 2. Transformer Count (Redundancy)
    // Inferred from "circuits" or explicit "transformers" count (rare)
    if (tags.count("transformers")) {
        try { transformer_count = std::stoi(tags["transformers"]); } catch (...) {}
    } else {
        // Heuristic: Higher voltage usually implies redundancy
        if (high_voltage_kv >= 220.0f) transformer_count = 2; // Transmission Standard
        else transformer_count = 1;
    }

    // 3. Type Inference
    if (tags.count("substation")) {
        substation_type = tags["substation"]; // "transmission", "traction", "converter"
    } else {
        if (high_voltage_kv >= 220.0f) substation_type = "transmission";
        else if (high_voltage_kv >= 110.0f) substation_type = "distribution";
        else substation_type = "minor_distribution";
    }

    // 4. Capacity Estimation (MVA)
    // If explicit rating is missing, guess based on Voltage Class * Transformer Count.
    // Rule of Thumb:
    // 380kV Transfo ~ 600 MVA each
    // 220kV Transfo ~ 200 MVA each
    // 110kV Transfo ~ 40-60 MVA each
    
    if (tags.count("rating")) {
        try { total_capacity_mva = std::stof(tags["rating"]); } catch (...) {}
    } else {
        float base_mva = 40.0f;
        if (high_voltage_kv >= 380.0f) base_mva = 600.0f;
        else if (high_voltage_kv >= 220.0f) base_mva = 200.0f;
        else if (high_voltage_kv >= 110.0f) base_mva = 60.0f;
        
        total_capacity_mva = base_mva * transformer_count;
    }
}

void PowerSubstation::update_status() {
    // 1. Calculate Apparent Power (MVA)
    // MW = MVA * Power Factor
    // MVA = MW / Power Factor
    float current_mva = 0.0f;
    if (power_factor > 0.1f) current_mva = current_load_mw / power_factor;
    else current_mva = current_load_mw; // Resistive load fallback

    // 2. Overload Check
    if (current_mva > total_capacity_mva) {
        // Simulating rapid heating
        transformer_temp_c += 5.0f; // Fast rise
    } else {
        // Cooling (exponential decay to ambient 25C)
        if (transformer_temp_c > 45.0f) transformer_temp_c -= 1.0f;
    }

    // 3. Trip Logic (Protection Relay)
    // If temp exceeds limit (e.g., 90C oil temp), Breaker Trips.
    if (transformer_temp_c > 90.0f) {
        is_tripped = true;
        current_load_mw = 0.0f; // Blackout
    } else {
        // Auto-reclose logic (if load drops and temp cools)
        if (is_tripped && transformer_temp_c < 60.0f && current_mva < total_capacity_mva) {
            is_tripped = false; // Reset
        }
    }
}

bool PowerSubstation::has_spare_capacity(float additional_load_mw) const {
    if (is_tripped) return false;
    
    float current_mva = current_load_mw / power_factor;
    float additional_mva = additional_load_mw / power_factor;
    
    // Allow short-term overload up to 110% (1.1x)
    return (current_mva + additional_mva) <= (total_capacity_mva * 1.1f);
}