#include "PumpingStationChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- HYDRAULIC PHYSICS CONSTANTS ---
    constexpr float DEFAULT_NPSH_PSI = 20.0f;   // Min inlet pressure to avoid cavitation
    constexpr float VISCOSITY_PENALTY_FACTOR = 0.01f; // Efficiency loss per cSt
    constexpr float LEAK_SHUTDOWN_THRESHOLD = 0.8f; // Severity > 0.8 = Full Stop
}

PumpingStationChokePoint::PumpingStationChokePoint(int id, std::string name, double lat, double lon, 
                                                   float max_psi, float max_flow)
    : BaseChokePoint(id, name, "pumping_station", lat, lon), 
      max_pressure_psi(max_psi), 
      max_flow_rate_bpd(max_flow) {
    
    // Defaults
    total_pumps = 4;
    required_npsh_psi = DEFAULT_NPSH_PSI;
    
    discharge_pressure_psi = max_psi * 0.8f;
    suction_pressure_psi = max_psi * 0.2f; // Healthy inlet
    active_pumps = 4;
    fluid_viscosity_cst = 10.0f; // Light Crude default
    
    is_leaking = false;
    is_cavitating = false;
    is_maintenance = false;
    
    hydraulic_efficiency = 1.0f;
}

void PumpingStationChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. SCADA / Telemetry
    if (category == "telemetry" || category == "scada") {
        if (sig.contains("discharge_psi")) discharge_pressure_psi = sig["discharge_psi"].get<float>();
        if (sig.contains("suction_psi")) suction_pressure_psi = sig["suction_psi"].get<float>();
        if (sig.contains("active_pumps")) active_pumps = sig["active_pumps"].get<int>();
    }
    
    // 2. Fluid Properties (Batch Change)
    else if (category == "fluid" || category == "batch") {
        if (sig.contains("viscosity")) fluid_viscosity_cst = sig["viscosity"].get<float>();
    }

    // 3. Integrity / Maintenance
    else if (category == "integrity" || category == "leak") {
        float severity = sig.value("severity", 0.0f);
        is_leaking = (severity > 0.1f);
    }
    else if (category == "maintenance") {
        is_maintenance = sig.value("active", false);
    }

    last_update = sig.value("timestamp", 0LL);
    check_cavitation_physics();
}

void PumpingStationChokePoint::check_cavitation_physics() {
    // NPSH Check: Is there enough pressure at the inlet to prevent boiling?
    if (suction_pressure_psi < required_npsh_psi) {
        is_cavitating = true;
    } else {
        is_cavitating = false;
    }

    // Efficiency Calculation based on Viscosity
    // Higher viscosity = harder to pump = lower effective flow rate
    // Baseline is Water (1 cSt) ~ 1.0 efficiency
    // Heavy Crude (100 cSt) ~ significant penalty
    float penalty = (fluid_viscosity_cst - 1.0f) * VISCOSITY_PENALTY_FACTOR;
    hydraulic_efficiency = std::max(0.5f, 1.0f - penalty);
}

float PumpingStationChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Safety Shutdowns
    if (is_leaking) return 0.0f; // Environmental containment
    if (active_pumps == 0) return 0.0f;

    // 2. Cavitation Throttle
    // If cavitating, we must throttle down to 0% or very low to save pumps.
    // In reality, automation trips the pumps. We simulate 10% flow (gravity bypass?) or 0.
    if (is_cavitating) return 0.0f; 

    // 3. Mechanical Capacity
    float pump_ratio = (float)active_pumps / (float)total_pumps;
    
    // 4. Maintenance Derating
    if (is_maintenance) pump_ratio *= 0.5f;

    // 5. Viscosity Impact
    return pump_ratio * hydraulic_efficiency;
}

json PumpingStationChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"discharge_psi", discharge_pressure_psi},
            {"suction_psi", suction_pressure_psi},
            {"active_pumps", active_pumps},
            {"leaking", is_leaking},
            {"cavitating", is_cavitating}
        }},
        {"physics", {
            {"viscosity_cst", fluid_viscosity_cst},
            {"efficiency", hydraulic_efficiency},
            {"npsh_limit", required_npsh_psi}
        }},
        {"capacity", {
            {"max_psi", max_pressure_psi},
            {"max_bpd", max_flow_rate_bpd}
        }},
        {"throughput_mod", const_cast<PumpingStationChokePoint*>(this)->calculate_throughput_modifier()}
    };
}