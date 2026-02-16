#include "BridgeChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- PHYSICS CONSTANTS ---
    constexpr float RHO_AIR_DEFAULT = 1.225f; // kg/m^3 at sea level
    constexpr float GRAVITY = 9.81f;          // m/s^2
    
    // Truck parameters for Overturning Calculation
    // Standard Semi-Trailer
    constexpr float TRUCK_MASS_KG = 15000.0f; // Lightly loaded (worst case for tipping)
    constexpr float TRUCK_SIDE_AREA = 35.0f;  // m^2
    constexpr float TRUCK_WIDTH = 2.5f;       // m
    constexpr float TRUCK_CG_HEIGHT = 1.8f;   // m (Center of Gravity)
    constexpr float TRUCK_DRAG_COEFF = 1.3f;  // Cd for a boxy trailer side
    
    // Bridge Structural Constants
    // Empirically, mass scales with span squared for long bridges
    constexpr float BRIDGE_MASS_FACTOR = 15000.0f; // kg/m linear mass estimate
}

BridgeChokePoint::BridgeChokePoint(int id, std::string name, double lat, double lon, 
                                   float span, float width, float height)
    : BaseChokePoint(id, name, "bridge", lat, lon), 
      span_length_m(span), 
      deck_width_m(width), 
      clearance_height_m(height) {
    
    max_load_tons = 100.0f; // Default
    
    // State Defaults
    current_wind_speed_ms = 0.0f;
    air_density_kgm3 = RHO_AIR_DEFAULT;
    structural_health = 1.0f;
    is_maintenance = false;

    update_physics_limits();
}

void BridgeChokePoint::update_physics_limits() {
    // 1. Natural Frequency Estimation ($f_n$)
    // Fundamental frequency drops as span length increases.
    // Empirical approximation: f ~ 100 / L (Hz)
    // We scale by sqrt(structural_health) because stiffness ($k$) drops with damage ($\omega = \sqrt{k/m}$)
    if (span_length_m > 0) {
        natural_freq_hz = (100.0f / span_length_m) * std::sqrt(structural_health);
    } else {
        natural_freq_hz = 10.0f; // Stiff/Short bridge
    }
    float omega = 2.0f * M_PI * natural_freq_hz; // Angular frequency

    // 2. Critical Flutter Velocity ($U_{cr}$) - Selberg Approximation
    // U_cr = K * omega * Width
    // K is a complex function of mass/radius ratio, simplified here to 2.5 for plate decks.
    // Flutter is the point where aerodynamic damping becomes negative -> Collapse.
    // Width ($B$) provides aerodynamic stability.
    // Heuristic K factor for suspension bridges ~ 2.5 - 3.0
    float flutter_coeff = 2.5f; 
    critical_flutter_vel_ms = flutter_coeff * omega * deck_width_m;

    // 3. Vehicle Overturning Velocity ($U_{tip}$)
    // Moment Balance: Resisting (Gravity) vs Overturning (Wind)
    // M_resist = Mass * g * (Width/2)
    // M_wind = Force * Height_CG = (0.5 * rho * v^2 * Cd * Area) * Height_CG
    // Solve for v:
    float resisting_moment = TRUCK_MASS_KG * GRAVITY * (TRUCK_WIDTH / 2.0f);
    float wind_force_const = 0.5f * air_density_kgm3 * TRUCK_DRAG_COEFF * TRUCK_SIDE_AREA * TRUCK_CG_HEIGHT;
    
    if (wind_force_const > 0.001f) {
        vehicle_overturn_vel_ms = std::sqrt(resisting_moment / wind_force_const);
    } else {
        vehicle_overturn_vel_ms = 999.0f;
    }
}

void BridgeChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // Weather Signal (Wind & Density)
    if (category == "weather") {
        if (sig.contains("wind_speed_ms")) {
            current_wind_speed_ms = sig["wind_speed_ms"].get<float>();
        }
        if (sig.contains("temp_c")) {
            // Adjust air density: rho = P / (R * T)
            // Simplified: rho_new = rho_std * (288 / (273 + T))
            float temp_c = sig["temp_c"].get<float>();
            air_density_kgm3 = RHO_AIR_DEFAULT * (288.15f / (273.15f + temp_c));
            // Recalc physics because density changed
            update_physics_limits();
        }
    }
    
    // Seismic/Integrity Signal (Stiffness Loss)
    else if (category == "integrity" || category == "seismic") {
        float damage = sig.value("severity", 0.0f);
        structural_health -= damage;
        if (structural_health < 0.1f) structural_health = 0.1f; // Prevent div/0 or negative roots
        
        // Damage reduces stiffness, which lowers natural frequency and flutter limit
        update_physics_limits();
    }

    // Maintenance
    else if (category == "maintenance") {
        is_maintenance = sig.value("active", false);
    }

    last_update = sig.value("timestamp", 0LL);
}

float BridgeChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Structural Failure Limit (Flutter)
    // If wind speed exceeds critical flutter velocity, the bridge enters unstable oscillation.
    // It is effectively closed/destroying itself.
    if (current_wind_speed_ms > critical_flutter_vel_ms) {
        return 0.0f; 
    }

    // 2. Traffic Safety Limit (Overturning)
    // If wind speed allows trucks to tip over, we ban high-profile vehicles (Logistics stop).
    // Cars might still pass, but commercial throughput drops to near zero.
    if (current_wind_speed_ms > vehicle_overturn_vel_ms) {
        return 0.0f; // Trucks banned
    }

    // 3. Operational Throttle (Safety Buffer)
    // As wind approaches the tipping point, speed limits are reduced linearly.
    // Start throttling at 70% of tip speed.
    float wind_throttle = 1.0f;
    float safety_ratio = current_wind_speed_ms / vehicle_overturn_vel_ms;
    
    if (safety_ratio > 0.7f) {
        // Linearly reduce flow from 100% to 0% as we approach the limit
        wind_throttle = 1.0f - ((safety_ratio - 0.7f) / 0.3f);
    }

    // 4. Maintenance / Health
    float maint_factor = is_maintenance ? 0.5f : 1.0f;
    
    return structural_health * wind_throttle * maint_factor;
}

json BridgeChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"geometry", {
            {"span_m", span_length_m},
            {"width_m", deck_width_m},
            {"height_m", clearance_height_m}
        }},
        {"physics", {
            {"natural_freq_hz", natural_freq_hz},
            {"flutter_limit_ms", critical_flutter_vel_ms},
            {"overturn_limit_ms", vehicle_overturn_vel_ms},
            {"current_wind_ms", current_wind_speed_ms}
        }},
        {"health", structural_health},
        {"throughput_mod", const_cast<BridgeChokePoint*>(this)->calculate_throughput_modifier()}
    };
}