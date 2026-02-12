#include "DamChokePoint.hpp"
#include <iostream>
#include <algorithm>

DamChokePoint::DamChokePoint(int id, std::string name, double lat, double lon, float capacity)
    : BaseChokePoint(id, name, "dam", lat, lon), 
      max_capacity_m3(capacity) {
    
    // Defaults
    current_level_pct = 0.8f;   // 80% full is healthy
    structural_health = 1.0f;   // Structure is sound
    is_generating_power = true; // Turbines are spinning
    spillway_limit_m3s = 5000.0f; // Default generic limit
}

void DamChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Water Level (Satellite Altimetry)
    if (category == "water_level" || category == "environmental") {
        if (sig.contains("level_pct")) {
            current_level_pct = sig["level_pct"].get<float>();
        }
        else if (sig.contains("severity")) {
            // Severity 1.0 = Empty (Drought)
            current_level_pct = 1.0f - sig["severity"].get<float>();
        }
    }
    
    // 2. Integrity Signal (Structural Cracks / Erosion)
    else if (category == "integrity" || category == "seismic") {
        float damage = sig.value("severity", 0.0f);
        structural_health -= damage;
        if (structural_health < 0.0f) structural_health = 0.0f;
    }

    // 3. Operational Signal (Power Demand)
    else if (category == "operational") {
        is_generating_power = sig.value("generating", true);
    }

    last_update = sig.value("timestamp", 0LL);
}

float DamChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Catastrophic Failure (Dam Breach)
    // If the dam fails, the route is destroyed by the flood wave.
    if (structural_health < 0.2f) return 0.0f;

    // 2. Low Water (Drought / Filling Mode)
    // If the dam holds back water (level < 20%), the river downstream
    // becomes too shallow for heavy barges.
    if (current_level_pct < 0.2f) {
        // Linear drop-off: 20% level = 100% flow capability (conceptually)
        // 0% level = 0% flow capability
        return current_level_pct * 5.0f; 
    }

    // 3. High Water (Flood Release)
    // If the dam is dangerously full (> 95%), it must open spillways.
    // The turbulent current makes navigation dangerous/impossible.
    if (current_level_pct > 0.95f) {
        return 0.3f; // Severe throttling for safety
    }

    // 4. Standard Operation
    // If generating power, water is flowing consistently -> Good for navigation.
    // If NOT generating and level is average, they might be storing water -> Reduced flow.
    if (!is_generating_power && current_level_pct < 0.8f) {
        return 0.7f; // Moderate throttling
    }

    return 1.0f * structural_health;
}

json DamChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"level_pct", current_level_pct},
            {"health", structural_health},
            {"generating", is_generating_power}
        }},
        {"capacity_m3", max_capacity_m3},
        {"throughput_mod", const_cast<DamChokePoint*>(this)->calculate_throughput_modifier()}
    };
}