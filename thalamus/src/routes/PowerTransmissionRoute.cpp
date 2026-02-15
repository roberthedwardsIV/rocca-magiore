#include "PowerTransmissionRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
    // --- GRID PHYSICS CONSTANTS ---
    constexpr float SIL_BASE_IMPEDANCE_OHMS = 400.0f;   
    constexpr float CABLE_DERATING_FACTOR = 0.6f;      
    constexpr float HVDC_CAPACITY_BOOST = 1.5f;         
    constexpr float BUNDLE_CONDUCTOR_BOOST = 0.2f;   
    constexpr float MAX_SAFE_TEMP_C = 90.0f;           
    
    // Industrial Defaults
    constexpr float PF_MINE_INDUCTIVE = 0.85f;          
    constexpr float PF_SMELTER_ARC = 0.90f;             
    constexpr float PF_GRID_STANDARD = 0.95f;
}

// Constructor for initialization
PowerTransmissionRoute::PowerTransmissionRoute(long long id, std::string name)
    : BaseRoute(id, name, "power") {
    
    // Default values
    voltage_kv = 110.0f;
    frequency_hz = 50.0f;      
    circuits = 1;
    cables_per_phase = 1;      
    is_hvdc = false;
    is_underground = false;
    is_dedicated_feed = false;
    has_arc_furnace_load = false;
    base_power_factor = PF_GRID_STANDARD;
    current_load_mw = 0.0f;
    current_mvar = 0.0f;
    conductor_temp_c = 25.0f; 
    ambient_temp_c = 25.0f;
    is_tripped = false;
    thermal_limit_mva = 0.0f;
    effective_mw_capacity = 0.0f;
    surge_impedance_loading_mw = 0.0f;
}


// Direct updates to static fields from OSM tags
void PowerTransmissionRoute::parse_osm_tags() {
    // Voltage
    if (tags.count("voltage")) {
        std::string v_str = tags["voltage"];
        try {
            std::replace(v_str.begin(), v_str.end(), ';', ' ');
            std::stringstream ss(v_str);
            float max_v = 0.0f, temp_v;
            while (ss >> temp_v) { if (temp_v > max_v) max_v = temp_v; }
            
            // Normalize to kV
            voltage_kv = (max_v > 1000.0f) ? max_v / 1000.0f : max_v;
        } catch (...) { voltage_kv = 110.0f; }
    }

    // Circuts + Cables
    if (tags.count("circuits")) {
        try { circuits = std::stoi(tags["circuits"]); } catch (...) { circuits = 1; }
    } else if (tags.count("cables")) {
        try { 
            int c = std::stoi(tags["cables"]);
            circuits = std::max(1, c / 3); // 3 phases per circuit
        } catch (...) {}
    }

    // High voltage usually uses bundle conductors
    if (voltage_kv >= 380.0f) cables_per_phase = 4;
    else if (voltage_kv >= 220.0f) cables_per_phase = 2;
    else cables_per_phase = 1;

    // Line type
    if (tags.count("location")) {
        if (tags["location"] == "underground" || tags["location"] == "underwater") {
            is_underground = true;
        }
    }

    // Link to assets by location if matching
    if (name.find("Mine") != std::string::npos || name.find("Smelter") != std::string::npos) {
        is_dedicated_feed = true;
        if (name.find("Smelter") != std::string::npos) {
            has_arc_furnace_load = true;
            base_power_factor = PF_SMELTER_ARC;
        } else {
            base_power_factor = PF_MINE_INDUCTIVE;
        }
    }
    
    update_metrics();
}


// Updates to dynamic + calculated fields from Thalamus signaling
void PowerTransmissionRoute::update_metrics() {
    // Base Capacity (Surge Impedance Loading - SIL) 
    // SIL (MW) approx kV^2 / Z_surge (400 ohms overhead)
    float base_sil_mw = (voltage_kv * voltage_kv) / SIL_BASE_IMPEDANCE_OHMS;
    
    // Total Capacity = Base * Circuits
    float raw_capacity = base_sil_mw * circuits;

    // Bundle Conductor Boost 
    if (cables_per_phase > 1) {
        raw_capacity *= (1.0f + (BUNDLE_CONDUCTOR_BOOST * cables_per_phase));
    }

    // HVDC Efficiency
    if (is_hvdc) raw_capacity *= HVDC_CAPACITY_BOOST;

    // Thermal Limits
    float thermal_factor = is_underground ? CABLE_DERATING_FACTOR : 2.5f;
    
    // Ambient Temp Derating (Dynamic Line Rating)
    // Capacity drops 1% for every degree above 25C
    float temp_derating = 1.0f;
    if (ambient_temp_c > 25.0f) {
        temp_derating = 1.0f - ((ambient_temp_c - 25.0f) * 0.01f);
    }
    
    thermal_limit_mva = raw_capacity * thermal_factor * std::max(0.5f, temp_derating);

    // Effective Capacity
    // Real Power (MW) = Apparent Power (MVA) * Power Factor
    effective_mw_capacity = thermal_limit_mva * base_power_factor;

    // Arc Furnace Stability Check
    if (has_arc_furnace_load) {
        effective_mw_capacity *= 0.9f; 
    }
}


// Main signal routing function
void PowerTransmissionRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::string category = sig.value("category", "none");

    // SCADA (Load / Temp)
    if (category == "scada" || category == "load") {
        if (sig.contains("mw")) current_load_mw = sig["mw"].get<float>();
        if (sig.contains("mvar")) {
            current_mvar = sig["mvar"].get<float>();
            // PF = MW / sqrt(MW^2 + MVAR^2)
            float mva = std::sqrt((current_load_mw * current_load_mw) + (current_mvar * current_mvar));
            if (mva > 0) base_power_factor = current_load_mw / mva;
        }
        if (sig.contains("conductor_temp_c")) conductor_temp_c = sig["conductor_temp_c"].get<float>();
    }

    // Protection
    else if (category == "protection" || category == "trip") {
        bool tripped = sig.value("tripped", false);
        if (tripped) {
            is_tripped = true;
            current_load_mw = 0.0f;
        } else {
            is_tripped = false;
        }
    }

    // Weather 
    else if (category == "weather") {
        if (sig.contains("temp_c")) ambient_temp_c = sig["temp_c"].get<float>();
    }

    // Dynamic Overload Trip Logic
    if (conductor_temp_c > MAX_SAFE_TEMP_C) {
        is_tripped = true;
        current_load_mw = 0.0f;
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}


// JSON packager for archival
json PowerTransmissionRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(route_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"load_mw", current_load_mw},
        {"limit_mva", thermal_limit_mva},
        {"effective_mw_cap", effective_mw_capacity},
        {"power_factor", base_power_factor},
        {"is_tripped", is_tripped},
        {"conductor_temp_c", conductor_temp_c},
        {"last_update", last_update}
    };
}