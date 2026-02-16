#include "TunnelChokePoint.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- TUNNEL PHYSICS CONSTANTS ---
    
    // Seismic / Structural
    constexpr float DEFAULT_DESIGN_PGA = 0.4f;    // 0.4g (High resistance standard)
    constexpr float FATIGUE_RATE_PER_YEAR = 0.02f;// Natural aging
    
    // Hydraulics (Flooding)
    constexpr float MAX_FORDING_DEPTH_CAR = 0.30f; // Meters
    constexpr float MAX_FORDING_DEPTH_TRUCK = 0.60f;
    constexpr float DEFAULT_PUMP_CAPACITY = 500.0f; // m3/h
    
    // Ventilation (Air Quality)
    constexpr float EMISSION_PER_VEHICLE = 0.05f; // Arbitrary CO units per vehicle/km
    constexpr float PISTON_EFFECT_EFFICIENCY = 0.4f; // % of traffic speed converted to airflow
    constexpr float SAFE_CO_THRESHOLD = 50.0f;    // PPM threshold
}

TunnelChokePoint::TunnelChokePoint(int id, std::string name, double lat, double lon, float length)
    : BaseChokePoint(id, name, "tunnel", lat, lon), length_meters(length) {
    
    // Defaults
    cross_section_area_m2 = 50.0f; // 2-lane standard
    design_pga_g = DEFAULT_DESIGN_PGA;
    pump_capacity_m3h = DEFAULT_PUMP_CAPACITY;

    current_structural_damage = 0.0f;
    accumulated_fatigue = 0.0f;
    
    water_depth_m = 0.0f;
    inflow_rate_m3h = 0.0f;
    fans_operational = true;
    
    current_traffic_vph = 0.0f;
}

void TunnelChokePoint::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(choke_mutex);
    std::string category = sig.value("category", "none");

    // 1. Seismic Event (Structural Damage Physics)
    if (category == "seismic" || category == "integrity") {
        if (sig.contains("pga_g")) {
            float event_pga = sig["pga_g"].get<float>();
            
            // Fragility Function: No damage if Event < Design.
            // Exponential damage if Event > Design.
            if (event_pga > design_pga_g) {
                float excess_force = event_pga - design_pga_g;
                // Damage scales with the square of excess force (energy)
                float new_damage = std::pow(excess_force, 2.0f); 
                current_structural_damage += new_damage;
            }
        }
        // Maintenance repairs
        if (sig.contains("repair_pct")) {
            current_structural_damage -= sig["repair_pct"].get<float>();
            if (current_structural_damage < 0.0f) current_structural_damage = 0.0f;
        }
    }
    
    // 2. Hydrology (Flooding Physics)
    else if (category == "hydrology" || category == "water") {
        if (sig.contains("inflow_rate")) inflow_rate_m3h = sig["inflow_rate"].get<float>();
        if (sig.contains("pump_capacity")) pump_capacity_m3h = sig["pump_capacity"].get<float>();
        
        // Calculate Water Level Change (Integration)
        // Assume 1-hour tick for simplicity, or derive from timestamp delta
        // dVol = (In - Out) * dt
        float net_flow = inflow_rate_m3h - pump_capacity_m3h;
        
        // Only accumulate if pumps are overwhelmed
        if (net_flow > 0) {
            // Volume to Depth: Depth = Vol / Area (Approx Area = Length * Width)
            // Width approx 10m
            float floor_area = length_meters * 10.0f; 
            float depth_increase = net_flow / floor_area; 
            water_depth_m += depth_increase;
        } else {
            // Drainage (cannot go below 0)
            float floor_area = length_meters * 10.0f;
            float depth_decrease = std::abs(net_flow) / floor_area;
            water_depth_m -= depth_decrease;
            if (water_depth_m < 0.0f) water_depth_m = 0.0f;
        }
    }

    // 3. Mechanical / Traffic
    else if (category == "mechanical") {
        if (sig.contains("fans_active")) fans_operational = sig["fans_active"].get<bool>();
    }
    else if (category == "traffic") {
        if (sig.contains("flow_vph")) current_traffic_vph = sig["flow_vph"].get<float>();
    }

    last_update = sig.value("timestamp", 0LL);
}

bool TunnelChokePoint::is_air_quality_safe() const {
    if (fans_operational) return true;

    // Piston Effect Physics (Natural Ventilation)
    // Can the moving cars push enough air to clear their own exhaust?
    // Critical Length L_crit proportional to traffic speed / emission rate.
    // Heuristic: Without fans, tunnels > 500m cannot self-ventilate in congestion.
    
    if (length_meters < 500.0f) return true; // Short tunnels are self-clearing
    if (current_traffic_vph < 100.0f) return true; // Very low traffic is safe
    
    return false; // Long tunnel + No Fans + Traffic = Toxic
}

float TunnelChokePoint::calculate_throughput_modifier() {
    std::lock_guard<std::mutex> lock(choke_mutex);

    // 1. Structural Collapse
    // If damage > 50%, tunnel is structurally unsafe.
    if (current_structural_damage > 0.5f) return 0.0f;

    // 2. Hydraulic Obstruction (Fording Depth)
    if (water_depth_m > MAX_FORDING_DEPTH_TRUCK) return 0.0f; // Total blockage
    if (water_depth_m > MAX_FORDING_DEPTH_CAR) return 0.2f;   // Only heavy trucks, slow crawl

    // 3. Ventilation Safety
    if (!is_air_quality_safe()) return 0.0f; // Forced closure for life safety

    // 4. Degradation Factor
    // Minor damage or fatigue slows traffic (inspections, rough surface)
    float health_factor = 1.0f - current_structural_damage;
    
    return health_factor;
}

json TunnelChokePoint::get_json_state() const {
    std::lock_guard<std::mutex> lock(choke_mutex);
    return {
        {"id", id},
        {"type", entity_type},
        {"name", name},
        {"physics", {
            {"design_pga", design_pga_g},
            {"water_depth_m", water_depth_m},
            {"inflow_m3h", inflow_rate_m3h},
            {"fans_ok", fans_operational}
        }},
        {"damage", current_structural_damage},
        {"throughput_mod", const_cast<TunnelChokePoint*>(this)->calculate_throughput_modifier()}
    };
}