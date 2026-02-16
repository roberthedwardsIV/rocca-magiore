#ifndef TUNNEL_CHOKEPOINT_HPP
#define TUNNEL_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class TunnelChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL DIMENSIONS (Static) ---
    float length_meters;
    float cross_section_area_m2;
    float design_pga_g;        // Design Peak Ground Acceleration (Resistance)
    float pump_capacity_m3h;   // Drainage rate

    // --- DYNAMIC STATE ---
    float current_structural_damage; // 0.0 (Perfect) -> 1.0 (Collapsed)
    float accumulated_fatigue;       // Micro-cracks / wear
    
    // --- ENVIRONMENTAL STATE ---
    float water_depth_m;             // Current flood level
    float inflow_rate_m3h;           // Water entering (leak/rain)
    bool fans_operational;           // Mechanical status
    
    // --- TRAFFIC LOAD (Input for Physics) ---
    float current_traffic_vph;       // Vehicles per hour (Source of CO/Heat)

    // Constructor
    TunnelChokePoint(int id, std::string name, double lat, double lon, float length);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates flow based on:
    // 1. Water Depth (Tire limits)
    // 2. Air Quality (Piston Effect vs. Demand)
    // 3. Structural Safety Factor (Design PGA vs Event PGA)
    float calculate_throughput_modifier() override;

    // Listens for 'seismic' (PGA), 'hydrology' (inflow), 'traffic', 'mechanical'
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    // Calculates if natural airflow (Piston Effect) is sufficient for current traffic
    bool is_air_quality_safe() const;
};

#endif