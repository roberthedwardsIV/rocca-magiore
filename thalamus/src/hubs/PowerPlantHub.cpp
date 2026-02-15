#include "PowerPlantHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- GENERATION PHYSICS CONSTANTS ---
    
    // Ramp Rates (% of Capacity per Minute)
    constexpr float RAMP_PCT_NUCLEAR = 0.01f;   // 1% min (Xenon constraint)
    constexpr float RAMP_PCT_COAL = 0.03f;      // 3% min (Thermal inertia)
    constexpr float RAMP_PCT_CCGT = 0.08f;      // 8% min (Combined Cycle Gas)
    constexpr float RAMP_PCT_OCGT = 0.20f;      // 20% min (Open Cycle Peaker)
    constexpr float RAMP_PCT_HYDRO = 0.50f;     // 50% min (Valve limit)

    // Heat Rates (MMBtu/MWh) - Inverse of Efficiency
    // Lower heat rate = Higher efficiency
    constexpr float HR_NUCLEAR = 10.46f;        // ~33% eff
    constexpr float HR_COAL_SUBBIT = 10.0f;     // ~34% eff
    constexpr float HR_GAS_CCGT = 7.0f;         // ~48% eff (High efficiency)
    constexpr float HR_GAS_OCGT = 11.0f;        // ~31% eff (Peaker)

    // Wind Turbine Physics (Standard 2MW onshore curve)
    constexpr float WIND_CUT_IN_MS = 3.0f;      // Starts spinning
    constexpr float WIND_RATED_MS = 12.0f;      // Max power reached
    constexpr float WIND_CUT_OUT_MS = 25.0f;    // Safety brake (Too fast)
    
    // Solar Physics
    constexpr float SOLAR_REF_IRRADIANCE = 1000.0f; // Standard Test Conditions (W/m2)
}

PowerPlantHub::PowerPlantHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "power_plant", lat, lon) {
    
    // Default Infrastructure (Small Gas Peaker)
    max_output_mw = 50.0f;
    min_stable_load_mw = 10.0f; 
    ramp_rate_mw_min = 5.0f;
    is_dispatchable = true;
    
    fuel_source = "gas";
    gen_technology = "ocgt";
    base_heat_rate = HR_GAS_OCGT;

    // Default State
    current_output_mw = 0.0f;
    target_output_mw = 0.0f;
    fuel_inventory = 1000.0f; // Arbitrary units (pressure/tons)
    
    // Weather
    wind_speed_ms = 0.0f;
    solar_irradiance = 0.0f;

    capacity_factor = 0.0f;
    fuel_burn_rate = 0.0f;
    is_tripped = false;
}

void PowerPlantHub::parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) {
    // 1. Fuel Source & Tech
    if (tags.count("generator:source")) fuel_source = tags.at("generator:source");
    else if (tags.count("power_source")) fuel_source = tags.at("power_source");

    if (tags.count("generator:method")) gen_technology = tags.at("generator:method");

    // 2. Capacity
    if (tags.count("plant:output:electricity")) {
        try {
            std::string s = tags.at("plant:output:electricity");
            if (s.find("MW") != std::string::npos) max_output_mw = std::stof(s.substr(0, s.find("MW")));
            else max_output_mw = std::stof(s);
        } catch (...) {}
    } 
    else {
        // Fallback Capacity Estimates
        if (fuel_source == "nuclear") max_output_mw = 1000.0f;
        else if (fuel_source == "coal") max_output_mw = 500.0f;
        else if (fuel_source == "wind") max_output_mw = 20.0f; // Small farm
        else max_output_mw = 50.0f;
    }

    // 3. Assign Physics Parameters
    if (fuel_source == "nuclear") {
        is_dispatchable = true;
        min_stable_load_mw = max_output_mw * 0.50f;
        ramp_rate_mw_min = max_output_mw * RAMP_PCT_NUCLEAR;
        base_heat_rate = HR_NUCLEAR;
    } 
    else if (fuel_source == "coal") {
        is_dispatchable = true;
        min_stable_load_mw = max_output_mw * 0.30f;
        ramp_rate_mw_min = max_output_mw * RAMP_PCT_COAL;
        base_heat_rate = HR_COAL_SUBBIT;
    }
    else if (fuel_source == "gas") {
        is_dispatchable = true;
        if (gen_technology == "combined_cycle") {
            min_stable_load_mw = max_output_mw * 0.40f;
            ramp_rate_mw_min = max_output_mw * RAMP_PCT_CCGT;
            base_heat_rate = HR_GAS_CCGT;
        } else { // OCGT / Peaker
            min_stable_load_mw = max_output_mw * 0.10f;
            ramp_rate_mw_min = max_output_mw * RAMP_PCT_OCGT;
            base_heat_rate = HR_GAS_OCGT;
        }
    }
    else if (fuel_source == "hydro") {
        is_dispatchable = true;
        min_stable_load_mw = 0.0f;
        ramp_rate_mw_min = max_output_mw * RAMP_PCT_HYDRO;
    }
    else if (fuel_source == "wind" || fuel_source == "solar") {
        is_dispatchable = false; // Weather driven
        min_stable_load_mw = 0.0f;
        ramp_rate_mw_min = 9999.0f; // Instant reaction to weather
    }
}

void PowerPlantHub::update_metrics() {
    if (is_tripped) {
        current_output_mw = 0.0f;
        fuel_burn_rate = 0.0f;
        return;
    }

    // --- 1. RENEWABLE PHYSICS ---
    if (!is_dispatchable) {
        if (fuel_source == "wind") {
            // Wind Power Curve (Cubic Law)
            if (wind_speed_ms < WIND_CUT_IN_MS || wind_speed_ms > WIND_CUT_OUT_MS) {
                target_output_mw = 0.0f;
            } else if (wind_speed_ms >= WIND_RATED_MS) {
                target_output_mw = max_output_mw;
            } else {
                // P ~ v^3 between Cut-In and Rated
                float v_ratio = (wind_speed_ms - WIND_CUT_IN_MS) / (WIND_RATED_MS - WIND_CUT_IN_MS);
                target_output_mw = max_output_mw * std::pow(v_ratio, 3.0f);
            }
        } 
        else if (fuel_source == "solar") {
            // Solar Linear Response
            target_output_mw = max_output_mw * (solar_irradiance / SOLAR_REF_IRRADIANCE);
        }
        
        current_output_mw = std::min(target_output_mw, max_output_mw);
        fuel_burn_rate = 0.0f;
        return;
    }

    // --- 2. THERMAL PHYSICS (Dispatchable) ---
    
    // Check Fuel
    if (fuel_inventory <= 0.0f) {
        target_output_mw = 0.0f; // Starved
    }

    // Ramp Logic: Move Current towards Target constrained by Rate
    float delta = target_output_mw - current_output_mw;
    float max_change = ramp_rate_mw_min; // Assuming 1 min tick
    
    if (std::abs(delta) <= max_change) {
        current_output_mw = target_output_mw;
    } else {
        current_output_mw += (delta > 0 ? max_change : -max_change);
    }

    // Min Stable Load Constraint
    if (current_output_mw > 0.1f && current_output_mw < min_stable_load_mw) {
        // If commanded below min stable, logic dictates either shut down or hold min.
        // We assume hold min unless target is 0.
        if (target_output_mw < 0.1f) current_output_mw = 0.0f; // Shutting down
        else current_output_mw = min_stable_load_mw;
    }

    // Fuel Burn Calculation
    // Heat Rate usually worsens at partial load.
    // Simple curve: HR_actual = HR_base * (1.0 + 0.1 * (1 - LoadFactor))
    float load_factor = current_output_mw / std::max(1.0f, max_output_mw);
    float current_heat_rate = base_heat_rate * (1.0f + 0.2f * (1.0f - load_factor));
    
    // Fuel Burn (MMBtu/hr) = MW * HeatRate
    fuel_burn_rate = current_output_mw * current_heat_rate;
    
    // Capacity Factor
    capacity_factor = current_output_mw / std::max(1.0f, max_output_mw);
}

void PowerPlantHub::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(hub_mutex);
    std::string category = sig.value("category", "none");

    // Dispatch Instruction
    if (category == "dispatch") {
        if (sig.contains("target_mw")) set_dispatch_target(sig["target_mw"].get<float>());
    }
    
    // Weather Updates (Renewables)
    else if (category == "weather") {
        if (sig.contains("wind_speed_ms")) wind_speed_ms = sig["wind_speed_ms"].get<float>();
        if (sig.contains("irradiance_wm2")) solar_irradiance = sig["irradiance_wm2"].get<float>();
    }

    // Fuel Logistics
    else if (category == "logistics") {
        if (sig.contains("fuel_added")) fuel_inventory += sig["fuel_added"].get<float>();
        if (sig.contains("fuel_level")) fuel_inventory = sig["fuel_level"].get<float>();
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

void PowerPlantHub::set_dispatch_target(float mw) {
    if (!is_dispatchable) return; // Ignore commands for wind/solar
    target_output_mw = std::clamp(mw, 0.0f, max_output_mw);
}

json PowerPlantHub::get_json_state() const {
    std::lock_guard<std::mutex> lock(hub_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"source", fuel_source},
        {"tech", gen_technology},
        {"output_mw", current_output_mw},
        {"target_mw", target_output_mw},
        {"capacity_mw", max_output_mw},
        {"capacity_factor", capacity_factor},
        {"fuel_burn_rate", fuel_burn_rate},
        {"last_update", last_update}
    };
}