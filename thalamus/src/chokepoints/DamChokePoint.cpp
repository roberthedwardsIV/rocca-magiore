#include "DamChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- HYDRAULIC CONSTANTS ---
    constexpr float WEIR_COEFFICIENT = 1.7f; // Standard broad-crested weir metric constant
    constexpr float INTEGRATION_TIMESTEP_S = 3600.0f; // 1 Hour tick for volume updates
    constexpr float CRITICAL_FAILURE_HEALTH = 0.1f;   // Point of inevitable breach
}

DamChokePoint::DamChokePoint(int id, std::string name, double lat, double lon, 
                             float capacity, float height)
    : BaseChokePoint(id, name, "dam", lat, lon), 
      max_capacity_m3(capacity), 
      dam_height_m(height) {
    
    // Defaults: Assume spillway triggers at 90% height
    spillway_crest_level_m = dam_height_m * 0.9f;
    // Assume spillway is 20% of dam width (heuristic for initialization)
    spillway_width_m = 50.0f; 

    // Navigable Window (Heuristic baseline, can be updated by signals)
    // Needs enough water to float (~50 m3/s) but not a torrent (~2000 m3/s)
    min_navigable_flow_m3s = 50.0f;
    max_navigable_flow_m3s = 2000.0f;

    // Initial State: 80% Full, Equilibrium flow
    current_volume_m3 = max_capacity_m3 * 0.8f;
    current_inflow_m3s = 200.0f;
    turbine_outflow_m3s = 200.0f;
    spillway_outflow_m3s = 0.0f;
    structural_health = 1.0f;
    
    update_hydraulics();
}

void DamChokePoint::update_hydraulics() {
    // 1. Calculate Water Level (H)
    // Simplified V-H curve: Volume scales with Height^3 for a V-shaped valley.
    // H = H_max * (V / V_max)^(1/3)
    if (max_capacity_m3 > 0) {
        float vol_ratio = current_volume_m3 / max_capacity_m3;
        current_water_level_m = dam_height_m * std::pow(vol_ratio, 0.333f);
    }

    // 2. Calculate Spillway Discharge (Q_spill)
    // Physics: Flow over a weir Q = C * L * H^1.5
    if (current_water_level_m > spillway_crest_level_m) {
        float head_over_crest = current_water_level_m - spillway_crest_level_m;
        spillway_outflow_m3s = WEIR_COEFFICIENT * spillway_width_m * std::pow(head_over_crest, 1.5f);
    } else {
        spillway_outflow_m3s = 0.0f;
    }

    total_discharge_m3s = turbine_outflow_m3s + spillway_outflow_m3s;
}

void DamChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Hydrology (Inflow / Rain)
    if (category == "hydrology" || category == "weather") {
        if (sig.contains("inflow_m3s")) {
            current_inflow_m3s = sig["inflow_m3s"].get<float>();
        }
        
        // Mass Balance Update: V_new = V_old + (In - Out) * dt
        // This is a discrete step integration based on the signal arrival
        float net_flow = current_inflow_m3s - total_discharge_m3s;
        current_volume_m3 += (net_flow * INTEGRATION_TIMESTEP_S);
        
        // Physical Clamps
        if (current_volume_m3 < 0) current_volume_m3 = 0;
        // Water can physically exceed max_capacity_m3 during extreme floods (overtopping)
    }
    
    // 2. Operations (Turbines)
    else if (category == "scada" || category == "power") {
        if (sig.contains("discharge_m3s")) {
            turbine_outflow_m3s = sig["discharge_m3s"].get<float>();
        }
        // Dispatcher might send navigable limits updates based on river survey
        if (sig.contains("min_flow")) min_navigable_flow_m3s = sig["min_flow"];
        if (sig.contains("max_flow")) max_navigable_flow_m3s = sig["max_flow"];
    }

    // 3. Structural Integrity
    else if (category == "integrity" || category == "seismic") {
        float damage = sig.value("severity", 0.0f);
        structural_health -= damage;
        if (structural_health < 0.0f) structural_health = 0.0f;
    }

    last_update = sig.value("timestamp", 0LL);
    update_hydraulics();
}

float DamChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Structural Failure (Dam Breach)
    // If the dam fails, the river is destroyed/flooded. Route closed.
    if (structural_health < CRITICAL_FAILURE_HEALTH) return 0.0f;

    // 2. Drought Condition (Low Discharge)
    // If discharge is too low, river level downstream drops. Barges ground.
    if (total_discharge_m3s < min_navigable_flow_m3s) {
        // Linear penalty as flow drops below minimum
        // 50% of min flow = 50% capacity (light loading only)
        return std::max(0.0f, total_discharge_m3s / min_navigable_flow_m3s);
    }

    // 3. Flood Condition (High Discharge)
    // If discharge is too high, currents are unsafe for navigation.
    if (total_discharge_m3s > max_navigable_flow_m3s) {
        return 0.0f; // Unsafe / Closed
    }

    // 4. Overtopping Condition
    // Even if discharge is technically within limits, if water is overtopping the dam core 
    // (level > height), it's an emergency state.
    if (current_water_level_m > dam_height_m) {
        return 0.0f; // Emergency evacuation
    }

    // Ideal operations
    return 1.0f * structural_health;
}

json DamChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"storage", {
            {"volume_m3", current_volume_m3},
            {"capacity_m3", max_capacity_m3},
            {"level_m", current_water_level_m},
            {"height_m", dam_height_m}
        }},
        {"flow", {
            {"inflow", current_inflow_m3s},
            {"turbine_out", turbine_outflow_m3s},
            {"spillway_out", spillway_outflow_m3s},
            {"total_out", total_discharge_m3s}
        }},
        {"limits", {
            {"min_nav", min_navigable_flow_m3s},
            {"max_nav", max_navigable_flow_m3s}
        }},
        {"health", structural_health},
        {"throughput_mod", const_cast<DamChokePoint*>(this)->calculate_throughput_modifier()}
    };
}