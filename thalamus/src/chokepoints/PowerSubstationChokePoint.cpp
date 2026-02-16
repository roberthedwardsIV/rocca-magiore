#include "PowerSubstationChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- THERMAL PHYSICS CONSTANTS ---
    constexpr float RATED_TEMP_RISE = 55.0f;    // C rise at 100% load
    constexpr float MAX_SAFE_TEMP = 110.0f;     // Trip threshold (Oil limit)
    constexpr float WARNING_TEMP = 95.0f;       // Derating threshold
    constexpr float DEFAULT_TIME_CONSTANT = 2.0f; // Hours (Smaller subs heat faster)
    
    // Simulation Tick (Assumed 1 minute updates)
    constexpr float DT_HOURS = 1.0f / 60.0f;
}

PowerSubstationChokePoint::PowerSubstationChokePoint(int id, std::string name, double lat, double lon, 
                                                     float voltage, float capacity_mva)
    : BaseChokePoint(id, name, "substation_cp", lat, lon), 
      max_voltage_kv(voltage), 
      max_capacity_mva(capacity_mva) {
    
    // Defaults
    thermal_time_constant_h = DEFAULT_TIME_CONSTANT;
    current_load_mva = 0.0f;
    core_temp_c = 40.0f; // Warm start
    ambient_temp_c = 25.0f;
    
    is_tripped = false;
    is_maintenance = false;
    
    load_factor = 0.0f;
    voltage_pu = 1.0f;
}

void PowerSubstationChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. SCADA (Load)
    if (category == "scada" || category == "grid") {
        if (sig.contains("load_mva")) current_load_mva = sig["load_mva"].get<float>();
        if (sig.contains("voltage_pu")) voltage_pu = sig["voltage_pu"].get<float>();
        if (sig.contains("temp_c")) core_temp_c = sig["temp_c"].get<float>(); // Sensor truth overrides model
    }
    
    // 2. Weather
    else if (category == "weather") {
        if (sig.contains("temp_c")) ambient_temp_c = sig["temp_c"].get<float>();
    }

    // 3. Protection / Maintenance
    else if (category == "protection") {
        if (sig.contains("tripped")) is_tripped = sig["tripped"].get<bool>();
    }
    else if (category == "maintenance") {
        is_maintenance = sig.value("active", false);
    }

    last_update = sig.value("timestamp", 0LL);
    update_thermal_model();
}

void PowerSubstationChokePoint::update_thermal_model() {
    // 1. Calculate Load Factor
    load_factor = current_load_mva / std::max(1.0f, max_capacity_mva);
    
    // 2. Calculate Steady-State Target Temperature
    // T_target = Ambient + (Rated_Rise * Load_Factor^2)
    // Squared because resistive heating P = I^2 * R
    float target_temp = ambient_temp_c + (RATED_TEMP_RISE * std::pow(load_factor, 2.0f));
    
    // 3. Update Temperature (Exponential Approach)
    // T_new = T_old + (T_target - T_old) * (dt / Tau)
    float delta_temp = (target_temp - core_temp_c) * (DT_HOURS / thermal_time_constant_h);
    core_temp_c += delta_temp;

    // 4. Trip Logic
    if (core_temp_c > MAX_SAFE_TEMP) {
        is_tripped = true;
        current_load_mva = 0.0f; // Load shed
    } else if (is_tripped && core_temp_c < 70.0f) {
        // Hysteresis reset (requires manual intervention IRL, but auto-reset for sim)
        is_tripped = false;
    }
}

float PowerSubstationChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Hard Failures
    if (is_tripped) return 0.0f;
    if (is_maintenance) return 0.0f; // Assumes N-0 redundancy for a chokepoint

    // 2. Thermal Derating
    // If temp is high, we must throttle throughput to prevent a trip.
    if (core_temp_c > WARNING_TEMP) {
        // Linearly decrease capacity from 100% to 0% as temp goes from 95C to 110C
        float margin = MAX_SAFE_TEMP - WARNING_TEMP;
        float excess = core_temp_c - WARNING_TEMP;
        float thermal_factor = 1.0f - (excess / margin);
        return std::max(0.0f, thermal_factor);
    }

    // 3. Voltage Stability Derating
    // If voltage sags below 0.95 pu, efficiency drops. Below 0.90 pu is critical.
    if (voltage_pu < 0.90f) return 0.5f; // Brownout conditions
    if (voltage_pu < 0.95f) return 0.8f; // Stressed grid

    return 1.0f;
}

json PowerSubstationChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"capacity_mva", max_capacity_mva},
        {"status", {
            {"load_mva", current_load_mva},
            {"voltage_pu", voltage_pu},
            {"temp_c", core_temp_c},
            {"tripped", is_tripped}
        }},
        {"physics", {
            {"ambient_c", ambient_temp_c},
            {"load_factor", load_factor}
        }},
        {"throughput_mod", const_cast<PowerSubstationChokePoint*>(this)->calculate_throughput_modifier()}
    };
}