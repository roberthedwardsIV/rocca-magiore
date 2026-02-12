#include "CanalLockChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

CanalLockChokePoint::CanalLockChokePoint(int id, std::string name, double lat, double lon, 
                                         float max_draft, float max_beam)
    : BaseChokePoint(id, name, "canal_lock", lat, lon), 
      max_draft_meters(max_draft), 
      max_beam_meters(max_beam) {
    
    // Defaults
    water_level = 1.0f;       // 1.0 = Full operating depth
    mechanical_health = 1.0f;
    queue_size = 0.0f;        // 0.0 = No wait
}

void CanalLockChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Environmental Signal (Drought / Water Table)
    if (category == "environmental" || category == "water_level") {
        if (sig.contains("level_pct")) {
            water_level = sig["level_pct"].get<float>();
        } else if (sig.contains("severity")) {
            // Severity 1.0 = Total Drought (Water = 0.0)
            water_level = 1.0f - sig["severity"].get<float>();
        }
    }
    
    // 2. Mechanical Signal (Gate Failure / Pump Issues)
    else if (category == "mechanical" || category == "integrity") {
        float damage = sig.value("severity", 0.0f);
        mechanical_health -= damage;
        if (mechanical_health < 0.0f) mechanical_health = 0.0f;
    }

    // 3. Logistics Signal (Queue / Backlog)
    else if (category == "congestion" || category == "logistics") {
        if (sig.contains("queue_factor")) {
            queue_size = sig["queue_factor"].get<float>();
        }
    }

    last_update = sig.value("timestamp", 0LL);
}

float CanalLockChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Mechanical Hard Stop
    // If gates are broken (< 30%), nothing moves.
    if (mechanical_health < 0.3f) return 0.0f;

    // 2. Water Level Constraint (The Drought Factor)
    // Canal locks rely on gravity/water volume. 
    // If water drops to 60%, big ships can't pass, cutting throughput by half.
    // If water drops below 30%, the lock is inoperable.
    float water_modifier = 1.0f;
    if (water_level < 0.3f) return 0.0f; 
    else if (water_level < 0.8f) {
        // Linearly scale down capability between 80% and 30% water
        // e.g. 0.55 water -> 0.5 throughput
        water_modifier = (water_level - 0.3f) / 0.5f; 
    }

    // 3. Congestion Penalty
    // High queues don't stop the lock, but they indicate system saturation.
    // We treat this as friction.
    float friction = std::max(0.0f, 1.0f - queue_size);

    return mechanical_health * water_modifier * friction;
}

json CanalLockChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"status", {
            {"mech_health", mechanical_health},
            {"water_level", water_level},
            {"queue", queue_size}
        }},
        {"limits", {
            {"max_draft", max_draft_meters},
            {"max_beam", max_beam_meters}
        }},
        {"throughput_mod", const_cast<CanalLockChokePoint*>(this)->calculate_throughput_modifier()}
    };
}