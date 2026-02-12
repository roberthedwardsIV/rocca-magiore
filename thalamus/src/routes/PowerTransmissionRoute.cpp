#include "PowerTransmissionRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>

PowerTransmissionRoute::PowerTransmissionRoute(long long id, std::string name)
    : BaseRoute(id, name, "power") {
    
    // Default Electrical Specs (Medium Voltage Dist.)
    voltage_kv = 110.0f;       // Standard regional transmission
    frequency_hz = 50.0f;      // EU Default
    circuits = 1;              // Single circuit
    cables_per_phase = 1;      // Single conductor
    is_hvdc = false;
    is_underground = false;

    // Default Dynamic State
    current_load_mw = 0.0f;
    reactive_load_mvar = 0.0f;
    conductor_temp_c = 25.0f;  // Ambient
    is_tripped = false;

    // Default Operational Limits
    thermal_limit_mw = 0.0f;   // Calculated later
    surge_impedance_loading_mw = 0.0f;
}

void PowerTransmissionRoute::parse_osm_tags() {
    // 1. Voltage (The #1 Capacity Driver)
    if (tags.count("voltage")) {
        std::string v_str = tags["voltage"];
        try {
            // Handle complex tags "380000;110000" (Take the highest)
            std::replace(v_str.begin(), v_str.end(), ';', ' ');
            std::stringstream ss(v_str);
            float max_v = 0.0f;
            float temp_v;
            while (ss >> temp_v) {
                if (temp_v > max_v) max_v = temp_v;
            }
            // Convert to kV if raw number is huge (>1000)
            if (max_v > 1000.0f) voltage_kv = max_v / 1000.0f;
            else voltage_kv = max_v;

        } catch (...) { voltage_kv = 110.0f; }
    }

    // 2. Circuits (Number of lines on the tower)
    // OSM tag: "cables" (total wires) or "circuits" (independent 3-phase groups)
    if (tags.count("circuits")) {
        try { circuits = std::stoi(tags["circuits"]); } catch (...) { circuits = 1; }
    } else if (tags.count("cables")) {
        // Heuristic: 3 cables = 1 circuit, 6 = 2 circuits
        try { 
            int c = std::stoi(tags["cables"]);
            circuits = c / 3;
            if (circuits < 1) circuits = 1; 
        } catch (...) {}
    }

    // 3. Cables per Phase (Bundle Conductors)
    // tag "wires:single=2" or "wires=quad" is rare, so we infer from voltage.
    // >380kV usually uses bundles (2-4 wires) to reduce corona loss.
    if (voltage_kv >= 380.0f) cables_per_phase = 4;
    else if (voltage_kv >= 220.0f) cables_per_phase = 2;
    else cables_per_phase = 1;

    // 4. Frequency
    if (tags.count("frequency")) {
        try { frequency_hz = std::stof(tags["frequency"]); } catch (...) {}
    }

    // 5. Line Type (Cable vs Overhead)
    if (tags.count("location")) {
        if (tags["location"] == "underground" || tags["location"] == "underwater") {
            is_underground = true;
        }
    }
}

void PowerTransmissionRoute::update_metrics() {
    // 1. Base Capacity Calculation (Surge Impedance Loading - SIL)
    // SIL is approx V^2 / Z_surge (approx 250-400 ohms for overhead lines)
    // MW = (kV)^2 / 400 (Rough approximation for SIL)
    // Thermal limit is usually 2.5x SIL for short lines, 1.0x SIL for long lines (>300km)
    
    float base_sil_mw = (voltage_kv * voltage_kv) / 400.0f; 

    // Adjust for multiple circuits
    float total_capacity = base_sil_mw * circuits;

    // Adjust for Bundle Conductors (Better cooling, lower inductance -> Higher capacity)
    if (cables_per_phase > 1) {
        total_capacity *= (1.0f + (0.2f * cables_per_phase)); // +20% per extra wire rough est.
    }

    // 2. HVDC Boost
    if (is_hvdc) {
        // HVDC doesn't suffer from reactive losses, higher effective capacity
        total_capacity *= 1.5f; 
    }

    // 3. Thermal Derating (Ambient Temp)
    // If it's hot (35C+), lines sag, capacity drops by ~20%
    // If it's cold (<10C), capacity increases (Dynamic Line Rating)
    float temp_factor = 1.0f;
    if (conductor_temp_c > 35.0f) temp_factor = 0.8f;
    else if (conductor_temp_c < 5.0f) temp_factor = 1.2f;

    thermal_limit_mw = total_capacity * 2.5f * temp_factor; // Thermal limit usually >> SIL

    // 4. Protection Trip Logic
    if (current_load_mw > thermal_limit_mw) {
        // Simulate a trip after sustained overload (simplified)
        // In real grid, this happens in seconds. Here, we flag it.
        is_tripped = true;
        current_load_mw = 0.0f; // Blackout on this line
    }
}