#include "PowerSubstationHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace {
    // --- TRANSFORMER THERMAL PHYSICS ---
    constexpr float TEMP_RATED_RISE = 55.0f;    // Rated temp rise above ambient at 100% load
    constexpr float TEMP_TRIP_C = 110.0f;       // Oil temperature trip point
    constexpr float TEMP_ALARM_C = 90.0f;       // Warning threshold
    
    // Thermal Inertia (Time Constants)
    // Transformers are massive oil-filled tanks; they heat/cool slowly.
    constexpr float THERMAL_TIME_CONSTANT_H = 4.0f; // Hours to reach 63% of steady state
    
    // Simulation Tick (Assumed 1 minute updates from Dispatcher)
    constexpr float TICK_HOURS = 1.0f / 60.0f;
    
    // Default Ratings (MVA per Transformer unit estimate)
    constexpr float RATING_380KV = 600.0f;
    constexpr float RATING_220KV = 250.0f;
    constexpr float RATING_110KV = 60.0f;
    constexpr float RATING_DIST = 20.0f;
}

PowerSubstationHub::PowerSubstationHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "substation", lat, lon) {
    
    // Default Infrastructure
    high_voltage_kv = 110.0f;
    low_voltage_kv = 20.0f;
    transformer_count = 1;
    is_hvdc_converter = false;
    
    // Default Capacity
    total_capacity_mva = 40.0f;
    firm_capacity_mva = 0.0f;

    // Default Load
    current_load_mw = 0.0f;
    current_load_mvar = 0.0f;
    power_factor = 0.95f;

    // Default Thermal
    transformer_temp_c = 45.0f; // Operating temp
    ambient_temp_c = 25.0f;
    
    is_tripped = false;
    is_maintenance = false;
    
    substation_type = "distribution";
}

void PowerSubstationHub::parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) {
    // 1. Voltage Parsing
    if (tags.count("voltage")) {
        std::string v_str = tags.at("voltage");
        // Handle "380000;110000" format
        std::replace(v_str.begin(), v_str.end(), ';', ' ');
        std::stringstream ss(v_str);
        float v;
        std::vector<float> voltages;
        while (ss >> v) {
            if (v > 1000.0f) v /= 1000.0f; // Convert V to kV
            voltages.push_back(v);
        }
        if (!voltages.empty()) {
            std::sort(voltages.begin(), voltages.end());
            low_voltage_kv = voltages.front();
            high_voltage_kv = voltages.back();
        }
    }

    // 2. Transformer Count (Redundancy)
    if (tags.count("transformers")) {
        try { transformer_count = std::stoi(tags.at("transformers")); } catch (...) {}
    } else {
        // Heuristic: Transmission stations usually have redundancy
        if (high_voltage_kv >= 220.0f) transformer_count = 2;
        else transformer_count = 1;
    }

    // 3. Type Inference
    if (tags.count("substation")) {
        substation_type = tags.at("substation");
        if (substation_type == "converter") is_hvdc_converter = true;
    } else {
        if (high_voltage_kv >= 220.0f) substation_type = "transmission";
        else if (high_voltage_kv >= 110.0f) substation_type = "distribution";
        else substation_type = "minor";
    }

    // 4. Capacity Estimation (MVA)
    if (tags.count("rating")) {
        try { total_capacity_mva = std::stof(tags.at("rating")); } catch (...) {}
    } else {
        float unit_rating = RATING_DIST;
        if (high_voltage_kv >= 380.0f) unit_rating = RATING_380KV;
        else if (high_voltage_kv >= 220.0f) unit_rating = RATING_220KV;
        else if (high_voltage_kv >= 110.0f) unit_rating = RATING_110KV;
        
        total_capacity_mva = unit_rating * (float)transformer_count;
    }

    // Firm Capacity (N-1): Capacity remaining if one unit is lost
    if (transformer_count > 1) {
        firm_capacity_mva = total_capacity_mva * ((float)(transformer_count - 1) / (float)transformer_count);
    } else {
        firm_capacity_mva = 0.0f; // No redundancy
    }
}

void PowerSubstationHub::update_metrics() {
    // 1. Calculate Apparent Power (MVA)
    // S = sqrt(P^2 + Q^2)
    float apparent_load_mva = std::sqrt((current_load_mw * current_load_mw) + (current_load_mvar * current_load_mvar));
    
    // Update Power Factor for reference
    if (apparent_load_mva > 0.1f) power_factor = current_load_mw / apparent_load_mva;

    // 2. Thermal Physics (Exponential Lag)
    // Target Temp Rise = RatedRise * (Load / Capacity)^2
    // If Load = 1.2x (20% overload), Heat = 1.44x Rated.
    
    float load_ratio = apparent_load_mva / std::max(1.0f, total_capacity_mva);
    float steady_state_temp = ambient_temp_c + (TEMP_RATED_RISE * (load_ratio * load_ratio));
    
    // Apply Thermal Inertia: T_new = T_old + (T_target - T_old) * (1 - e^(-dt/tau))
    // Linear approximation for small dt: dTemp = (Target - Current) * (dt / Tau)
    float temp_change = (steady_state_temp - transformer_temp_c) * (TICK_HOURS / THERMAL_TIME_CONSTANT_H);
    transformer_temp_c += temp_change;

    // 3. Trip Logic
    if (transformer_temp_c > TEMP_TRIP_C) {
        is_tripped = true;
        current_load_mw = 0.0f; // Blackout
        current_load_mvar = 0.0f;
    } else {
        // Auto-reclose hysteresis (must cool down to Alarm level to reset)
        // In real life, manual intervention is needed, but we simulate auto-recloser logic here
        if (is_tripped && transformer_temp_c < TEMP_ALARM_C) {
            is_tripped = false; 
        }
    }
}

void PowerSubstationHub::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(hub_mutex);
    std::string category = sig.value("category", "none");

    // SCADA (Load Data)
    if (category == "scada" || category == "load") {
        if (sig.contains("mw")) current_load_mw = sig["mw"].get<float>();
        if (sig.contains("mvar")) current_load_mvar = sig["mvar"].get<float>();
        if (sig.contains("temp_c")) transformer_temp_c = sig["temp_c"].get<float>(); // Sensor override
    }
    
    // Weather (Ambient Cooling)
    else if (category == "weather") {
        if (sig.contains("temp_c")) ambient_temp_c = sig["temp_c"].get<float>();
    }

    // Protection/Trip Status
    else if (category == "protection") {
        if (sig.contains("tripped")) is_tripped = sig["tripped"].get<bool>();
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

bool PowerSubstationHub::has_n_minus_1_security() const {
    if (transformer_count <= 1) return false;
    
    // Check if current load fits within Firm Capacity
    float current_mva = std::sqrt((current_load_mw * current_load_mw) + (current_load_mvar * current_load_mvar));
    return current_mva <= firm_capacity_mva;
}

json PowerSubstationHub::get_json_state() const {
    std::lock_guard<std::mutex> lock(hub_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"sub_type", substation_type},
        {"voltages", {high_voltage_kv, low_voltage_kv}},
        {"capacity_mva", total_capacity_mva},
        {"firm_capacity_mva", firm_capacity_mva},
        {"load_mw", current_load_mw},
        {"load_mva", std::sqrt(pow(current_load_mw, 2) + pow(current_load_mvar, 2))},
        {"temp_c", transformer_temp_c},
        {"is_tripped", is_tripped},
        {"n_minus_1_secure", has_n_minus_1_security()},
        {"last_update", last_update}
    };
}