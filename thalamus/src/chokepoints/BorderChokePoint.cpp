#include "BorderChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

BorderChokePoint::BorderChokePoint(int id, std::string name, double lat, double lon, 
                                   std::string c1, std::string c2)
    : BaseChokePoint(id, name, "border", lat, lon), country_a(c1), country_b(c2) {
    
    // Defaults
    political_friction = 0.0f;      // 0.0 = Open/Ally
    customs_delay_hours = 2.0f;     // Standard processing time
    is_border_closed = false;
}

void BorderChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Political Signal (Sanctions / Diplomatic Tensions)
    if (category == "political") {
        // Severity 0.0 -> 1.0
        float tension = sig.value("severity", 0.0f);
        political_friction = tension;
        
        // Auto-close if relations break down completely
        if (political_friction > 0.9f) is_border_closed = true;
    }
    
    // 2. Logistics Signal (Queue Times / Strikes)
    else if (category == "logistics" || category == "customs") {
        if (sig.contains("wait_time_hours")) {
            customs_delay_hours = sig["wait_time_hours"].get<float>();
        }
        // Strikes often come as "severity" which we map to delays
        else if (sig.contains("severity")) {
            // A severe strike adds massive delays (e.g., +24 to +72 hours)
            customs_delay_hours = 2.0f + (sig["severity"].get<float>() * 48.0f);
        }
    }

    // 3. Binary Status Override (War / Pandemic)
    else if (category == "closure") {
        is_border_closed = sig.value("active", true);
    }

    last_update = sig.value("timestamp", 0LL);
}

float BorderChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Binary Closure (War, Pandemic, Embargo)
    if (is_border_closed) return 0.0f;

    // 2. Political Friction (Soft Barrier)
    // 0.0 friction = 1.0 flow. 0.8 friction = 0.2 flow.
    float politics_factor = 1.0f - political_friction;

    // 3. Customs Latency (Hard Delay)
    // We define "Base Throughput" as valid at 2 hours delay.
    // As delay approaches 24h+, throughput drops asymptotically.
    // Formula: Standard / (Standard + Excess_Delay)
    float base_standard = 4.0f; // 4 hour buffer is "fine"
    float logistics_factor = base_standard / (base_standard + std::max(0.0f, customs_delay_hours - 2.0f));

    return std::clamp(politics_factor * logistics_factor, 0.0f, 1.0f);
}

json BorderChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"countries", {country_a, country_b}},
        {"friction", political_friction},
        {"customs_delay_hours", customs_delay_hours},
        {"is_closed", is_border_closed},
        {"throughput_mod", const_cast<BorderChokePoint*>(this)->calculate_throughput_modifier()}
    };
}