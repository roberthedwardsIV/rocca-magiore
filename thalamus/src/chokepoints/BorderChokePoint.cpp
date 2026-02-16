#include "BorderChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- BORDER PROCESSING CONSTANTS ---
    
    // Time per Vehicle (Minutes)
    constexpr float TIME_PASS_THROUGH = 0.5f;   // Slow roll (Schengen)
    constexpr float TIME_DOCUMENT_CHECK = 2.0f; // Passport/Manifest scan
    constexpr float TIME_FULL_INSPECT = 45.0f;  // X-ray/Unload (Cargo scan)
    
    // Logistics Thresholds
    constexpr float MAX_ACCEPTABLE_WAIT_H = 12.0f; // Logistics actively reroute after this
    constexpr float CRITICAL_FAIL_WAIT_H = 48.0f;  // Perishables rot, JIT fails
}

BorderChokePoint::BorderChokePoint(int id, std::string name, double lat, double lon, 
                                   int lanes, std::string type,
                                   std::string c1, std::string c2)
    : BaseChokePoint(id, name, "border", lat, lon), 
      num_lanes(lanes), 
      border_type(type),
      country_a(c1),
      country_b(c2) {
    
    num_inspection_bays = std::max(1, lanes / 2);
    
    // Defaults based on type
    if (border_type == "schengen" || border_type == "open") {
        political_friction = 0.05f;
    } else if (border_type == "hostile") {
        political_friction = 0.9f;
    } else {
        political_friction = 0.5f; // Standard hard border
    }

    is_closed = false;
    traffic_volume_vph = 100.0f; // Default flow
    
    update_processing_physics();
}

void BorderChokePoint::update_processing_physics() {
    // 1. Determine Inspection Probability (P) from Friction
    // P = Friction^2 (Keeps low friction very low, ramps up fast at high friction)
    inspection_rate_pct = std::pow(political_friction, 1.5f); 
    if (inspection_rate_pct < 0.01f) inspection_rate_pct = 0.01f;

    // 2. Calculate Weighted Average Service Time (E[t])
    float baseline_time = (political_friction < 0.1f) ? TIME_PASS_THROUGH : TIME_DOCUMENT_CHECK;
    
    // Effective time per lane = Weighted avg of fast check vs full search
    float avg_time_min = ((1.0f - inspection_rate_pct) * baseline_time) + 
                         (inspection_rate_pct * TIME_FULL_INSPECT);
    
    avg_process_time_m = avg_time_min;

    // 3. Calculate Capacity (mu)
    effective_capacity_vph = (60.0f / avg_process_time_m) * (float)num_lanes;
}

void BorderChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // Politics (Friction / Embargo)
    if (category == "political" || category == "diplomacy") {
        if (sig.contains("friction")) political_friction = sig["friction"].get<float>();
        if (sig.contains("closed")) is_closed = sig["closed"].get<bool>();
    }
    
    // Traffic Flow (Arrival Rate)
    else if (category == "traffic" || category == "flow") {
        if (sig.contains("volume_vph")) traffic_volume_vph = sig["volume_vph"].get<float>();
    }

    last_update = sig.value("timestamp", 0LL);
    update_processing_physics();
    
    // Calculate Wait Time (MM1 Queue approx)
    if (is_closed) {
        estimated_wait_time_h = 999.0f;
    } else if (traffic_volume_vph >= effective_capacity_vph) {
        // Over-saturated: Delay depends on how long the rush lasts (Simulate 4h backlog)
        float excess_rate = traffic_volume_vph - effective_capacity_vph;
        float backlog = excess_rate * 4.0f; 
        estimated_wait_time_h = backlog / effective_capacity_vph;
    } else {
        // Under-saturated: Standard queuing
        float rho = traffic_volume_vph / effective_capacity_vph;
        float queue_time_m = (rho / (1.0f - rho)) * avg_process_time_m;
        estimated_wait_time_h = (queue_time_m + avg_process_time_m) / 60.0f;
    }
}

float BorderChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Binary Closure
    if (is_closed) return 0.0f;

    // 2. Logistic Viability Curve
    if (estimated_wait_time_h <= 2.0f) return 1.0f;
    if (estimated_wait_time_h >= CRITICAL_FAIL_WAIT_H) return 0.0f;

    // Linear degradation between 2h and 48h
    float penalty_range = CRITICAL_FAIL_WAIT_H - 2.0f;
    float current_penalty = estimated_wait_time_h - 2.0f;
    
    return 1.0f - (current_penalty / penalty_range);
}

json BorderChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"countries", {country_a, country_b}},
        {"status", {
            {"friction", political_friction},
            {"closed", is_closed},
            {"traffic_vph", traffic_volume_vph}
        }},
        {"physics", {
            {"inspect_prob", inspection_rate_pct},
            {"avg_process_time_m", avg_process_time_m},
            {"capacity_vph", effective_capacity_vph}
        }},
        {"wait_time_h", estimated_wait_time_h},
        {"throughput_mod", const_cast<BorderChokePoint*>(this)->calculate_throughput_modifier()}
    };
}