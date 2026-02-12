#ifndef BRIDGE_CHOKEPOINT_HPP
#define BRIDGE_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class BridgeChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL DIMENSIONS ---
    float length_meters;           // Longer spans = higher oscillation risk
    float clearance_height_meters; // Critical for maritime traffic underneath
    
    // --- STATIC CONSTRAINTS ---
    float max_weight_tons;
    float max_wind_speed_kmh;      // Calculated based on height/length
    
    // --- DYNAMIC STATE ---
    float structural_health;       // 0.0 (Collapsed) -> 1.0 (Perfect)
    float current_wind_speed;      // From weather signals
    bool is_maintenance_active;

    // Updated Constructor
    BridgeChokePoint(int id, std::string name, double lat, double lon, 
                     float weight_lim, float length, float clearance);

    // Calculates flow based on Wind + Health + Maintenance
    float calculate_throughput_modifier() override;

    // Standard Interface
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};

#endif