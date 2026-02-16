#include "RunwayChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- AVIATION PHYSICS CONSTANTS ---
    
    // Capacity Baselines (Single Runway Movements per Hour)
    // Derived from standard separation minima (3nm radar vs 10nm LVP)
    constexpr float CAP_VMC_OPTIMAL = 48.0f; // Visual conditions
    constexpr float CAP_IMC_CAT1 = 24.0f;    // Instrument conditions
    constexpr float CAP_LVP_CAT3 = 12.0f;    // Low Visibility Procedures
    
    // Visibility Thresholds (Meters RVR)
    constexpr float VIS_VMC_MIN = 5000.0f;
    constexpr float VIS_CAT1_MIN = 550.0f;   // ILS Cat I decision height
    constexpr float VIS_CAT3_MIN = 75.0f;    // ILS Cat III
    
    // Friction Coefficients (Mu)
    constexpr float MU_DRY = 0.8f;
    constexpr float MU_WET = 0.5f;
    constexpr float MU_ICE = 0.2f;
    
    // Aircraft Certification Limits (Generic Transport Category)
    constexpr float LIMIT_CROSSWIND_DRY = 35.0f; // Knots
    constexpr float LIMIT_CROSSWIND_WET = 25.0f;
    constexpr float LIMIT_CROSSWIND_ICE = 15.0f;
    constexpr float LIMIT_TAILWIND = 10.0f;      // Knots
}

RunwayChokePoint::RunwayChokePoint(int id, std::string name, double lat, double lon, 
                                   float length, float width, std::string surface)
    : BaseChokePoint(id, name, "runway", lat, lon), 
      length_meters(length), 
      width_meters(width), 
      surface_type(surface) {
    
    // Defaults: CAVOK (Ceiling and Visibility OK)
    visibility_meters = 9999.0f; 
    friction_coefficient = MU_DRY;
    crosswind_speed_kt = 0.0f;
    headwind_speed_kt = 0.0f;
    is_obstructed = false;
    is_maintenance = false;
    
    design_capacity_ph = CAP_VMC_OPTIMAL;
    current_capacity_ph = CAP_VMC_OPTIMAL;
    dynamic_crosswind_limit_kt = LIMIT_CROSSWIND_DRY;
}

void RunwayChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Weather Signal (METAR)
    if (category == "weather") {
        if (sig.contains("visibility")) visibility_meters = sig["visibility"].get<float>();
        if (sig.contains("friction")) friction_coefficient = sig["friction"].get<float>();
        
        // Wind vectors
        if (sig.contains("crosswind")) crosswind_speed_kt = std::abs(sig["crosswind"].get<float>());
        if (sig.contains("headwind")) headwind_speed_kt = sig["headwind"].get<float>();
        // Note: Negative headwind = tailwind
    }
    
    // 2. Obstruction (Crash / Disabled Aircraft)
    else if (category == "obstruction" || category == "incident") {
        is_obstructed = sig.value("active", true);
    }

    // 3. Maintenance (Resurfacing)
    else if (category == "maintenance") {
        is_maintenance = sig.value("active", true);
    }

    last_update = sig.value("timestamp", 0LL);
    update_aero_physics();
}

void RunwayChokePoint::update_aero_physics() {
    // --- 1. Calculate Dynamic Crosswind Limit ---
    // Interpolate limit based on friction (Mu)
    if (friction_coefficient >= MU_WET) {
        // Linear scaling between Dry (0.8) and Wet (0.5)
        float ratio = (friction_coefficient - MU_WET) / (MU_DRY - MU_WET);
        dynamic_crosswind_limit_kt = LIMIT_CROSSWIND_WET + ratio * (LIMIT_CROSSWIND_DRY - LIMIT_CROSSWIND_WET);
    } else {
        // Linear scaling between Wet (0.5) and Ice (0.2)
        float ratio = std::max(0.0f, (friction_coefficient - MU_ICE) / (MU_WET - MU_ICE));
        dynamic_crosswind_limit_kt = LIMIT_CROSSWIND_ICE + ratio * (LIMIT_CROSSWIND_WET - LIMIT_CROSSWIND_ICE);
    }

    // --- 2. Calculate Operational Capacity (Flow Rate) ---
    if (is_obstructed || is_maintenance) {
        current_capacity_ph = 0.0f;
        return;
    }

    // A. Wind Gate (Binary Safety Check)
    if (crosswind_speed_kt > dynamic_crosswind_limit_kt) {
        current_capacity_ph = 0.0f; // Winded off
        return;
    }
    if (headwind_speed_kt < -LIMIT_TAILWIND) {
        current_capacity_ph = 0.0f; // Wrong runway direction / too strong tailwind
        return;
    }

    // B. Visibility Gate (Separation Standards)
    float vis_capacity = CAP_VMC_OPTIMAL;
    if (visibility_meters < VIS_CAT3_MIN) {
        vis_capacity = 0.0f; // Below minima
    } else if (visibility_meters < VIS_CAT1_MIN) {
        vis_capacity = CAP_LVP_CAT3; // Massive separation required
    } else if (visibility_meters < VIS_VMC_MIN) {
        vis_capacity = CAP_IMC_CAT1; // Radar separation
    }

    // C. Braking Action Penalty
    // If friction is low, Runway Occupancy Time (ROT) increases because planes can't brake hard.
    // ROT increase = Capacity decrease.
    float braking_efficiency = 1.0f;
    if (friction_coefficient < MU_WET) {
        // Simple physics model: Braking dist proportional to 1/friction
        // Efficiency scales down.
        braking_efficiency = std::max(0.5f, friction_coefficient / MU_DRY);
    }

    current_capacity_ph = vis_capacity * braking_efficiency;
}

float RunwayChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);
    // Modifier is simply the ratio of Current Capacity to Design Capacity
    return current_capacity_ph / design_capacity_ph;
}

json RunwayChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"physics", {
            {"crosswind_kt", crosswind_speed_kt},
            {"limit_kt", dynamic_crosswind_limit_kt},
            {"friction", friction_coefficient},
            {"visibility_m", visibility_meters}
        }},
        {"capacity", {
            {"current_ph", current_capacity_ph},
            {"design_ph", design_capacity_ph}
        }},
        {"throughput_mod", const_cast<RunwayChokePoint*>(this)->calculate_throughput_modifier()}
    };
}