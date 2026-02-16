#include "CanalLockChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- MARITIME PHYSICS CONSTANTS ---
    constexpr float SEAWATER_DENSITY = 1.025f; // t/m3
    constexpr float BLOCK_COEFFICIENT = 0.75f; // Avg hull form efficiency for bulkers
    constexpr float REQUIRED_UKC_PCT = 0.10f;  // Under Keel Clearance (10% of draft)
    
    // Hydraulic Defaults
    constexpr float DEFAULT_LIFT_M = 10.0f;
    constexpr float GRAVITY_FILL_RATE_M3S = 50.0f; // Approx for medium lock
}

CanalLockChokePoint::CanalLockChokePoint(int id, std::string name, double lat, double lon, 
                                         float length, float width, float depth)
    : BaseChokePoint(id, name, "canal_lock", lat, lon), 
      chamber_length_m(length), 
      chamber_width_m(width), 
      sill_depth_m(depth) {
    
    // Defaults
    lift_height_m = DEFAULT_LIFT_M;
    design_cycle_time_m = 45.0f; 
    
    water_level_offset_m = 0.0f;
    fill_rate_m3s = GRAVITY_FILL_RATE_M3S;
    is_maintenance = false;

    // Calculate Design Baseline (The "1.0" standard)
    // Nominal draft includes the safety margin built-in to the sill depth
    float design_draft = sill_depth_m / (1.0f + REQUIRED_UKC_PCT);
    design_max_tonnage = estimate_max_dwt(design_draft);
    
    // Initial state
    current_usable_depth_m = design_draft;
    max_passable_tonnage = design_max_tonnage;
}

void CanalLockChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Hydrology (Water Level)
    if (category == "hydrology" || category == "water_level") {
        if (sig.contains("level_offset_m")) {
            water_level_offset_m = sig["level_offset_m"].get<float>();
        } 
        else if (sig.contains("level_pct")) {
            // Convert % fill to meters relative to sill
            float pct = sig["level_pct"].get<float>();
            float actual_depth = sill_depth_m * pct;
            water_level_offset_m = actual_depth - sill_depth_m;
        }
    }
    
    // 2. Mechanical Status
    else if (category == "maintenance" || category == "mechanical") {
        is_maintenance = sig.value("active", false);
        // If pumps are degraded, fill rate drops
        if (sig.contains("efficiency")) {
            float eff = sig["efficiency"].get<float>();
            fill_rate_m3s = GRAVITY_FILL_RATE_M3S * eff;
        }
    }

    last_update = sig.value("timestamp", 0LL);
    
    // Recalculate physics immediately
    float total_depth = sill_depth_m + water_level_offset_m;
    // Apply Safety Margin (UKC)
    // Usable Draft = Total Depth / 1.10
    current_usable_depth_m = std::max(0.0f, total_depth / (1.0f + REQUIRED_UKC_PCT));
    
    max_passable_tonnage = estimate_max_dwt(current_usable_depth_m);
}

// Physics approximation of vessel capacity
float CanalLockChokePoint::estimate_max_dwt(float draft) const {
    if (draft <= 0.5f) return 0.0f; // Canoe only
    
    // Displacement (Vol) = L * W * Draft * Cb
    // We assume the ship fits snugly in the lock (Panamax style)
    // Reduce L/W slightly for clearance
    float ship_l = chamber_length_m * 0.90f;
    float ship_w = chamber_width_m * 0.90f;
    
    float displaced_volume = ship_l * ship_w * draft * BLOCK_COEFFICIENT;
    float displacement_mass = displaced_volume * SEAWATER_DENSITY;
    
    // Deadweight (Cargo) is roughly 85% of Displacement for bulkers
    return displacement_mass * 0.85f;
}

float CanalLockChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Binary Mechanical Fail
    if (is_maintenance) return 0.0f;

    // 2. Hydraulic Cycle Time Impact
    // T_fill = Volume / Rate
    float lock_volume = chamber_length_m * chamber_width_m * lift_height_m;
    float fill_time_s = lock_volume / std::max(1.0f, fill_rate_m3s);
    float fill_time_m = fill_time_s / 60.0f;
    
    // Total Cycle = Entry + Fill + Exit
    // Assume Entry/Exit is constant (approx 20 mins) unless jammed
    float current_cycle_m = 20.0f + fill_time_m;
    
    float cycle_efficiency = design_cycle_time_m / current_cycle_m;
    if (cycle_efficiency > 1.0f) cycle_efficiency = 1.0f; // Can't go faster than design

    // 3. Tonnage Capacity Impact (The big factor)
    // If drought reduces draft, we lose the big ships.
    // Throughput Mass = Ship_Mass * Cycles_Per_Hour
    
    float tonnage_ratio = max_passable_tonnage / std::max(1.0f, design_max_tonnage);
    
    // Combine Cycle Speed * Ship Size
    return cycle_efficiency * tonnage_ratio;
}

json CanalLockChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"geometry", {
            {"length", chamber_length_m},
            {"width", chamber_width_m},
            {"sill_depth", sill_depth_m},
            {"lift", lift_height_m}
        }},
        {"status", {
            {"water_offset", water_level_offset_m},
            {"usable_draft", current_usable_depth_m},
            {"max_tonnage", max_passable_tonnage},
            {"fill_rate", fill_rate_m3s},
            {"maintenance", is_maintenance}
        }},
        {"throughput_mod", const_cast<CanalLockChokePoint*>(this)->calculate_throughput_modifier()}
    };
}