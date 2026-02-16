#ifndef DAM_CHOKEPOINT_HPP
#define DAM_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class DamChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL GEOMETRY (Static) ---
    float max_capacity_m3;      // Total reservoir volume
    float dam_height_m;         // Hydraulic head
    float spillway_width_m;     // Length of the overflow crest
    float spillway_crest_level_m; // Elevation where spilling starts (relative to base)

    // --- LOGISTICS CONSTRAINTS (Static) ---
    float min_navigable_flow_m3s; // Minimum discharge to keep downstream river deep enough
    float max_navigable_flow_m3s; // Maximum discharge before current is too strong for barges

    // --- DYNAMIC STATE ---
    float current_volume_m3;    // Current water storage
    float current_inflow_m3s;   // Water entering from upstream
    float turbine_outflow_m3s;  // Controlled release (Power Gen)
    float spillway_outflow_m3s; // Uncontrolled release (Flood)
    float structural_health;    // 0.0 (Breached) -> 1.0 (Solid)

    // --- CALCULATED METRICS ---
    float current_water_level_m; // Height of water column
    float total_discharge_m3s;   // Turbines + Spillway

    // Constructor
    DamChokePoint(int id, std::string name, double lat, double lon, 
                  float capacity, float height);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates flow modifier based on:
    // 1. Downstream conditions (Is discharge within the safe navigable window?)
    // 2. Structural Integrity (Is the dam about to fail?)
    float calculate_throughput_modifier() override;

    // Listens for 'hydrology' (inflow), 'scada' (turbines), 'seismic' (structure)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    // Approximates water level based on volume (V-H Curve)
    void update_hydraulics();
};

#endif