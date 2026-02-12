#ifndef TUNNEL_CHOKEPOINT_HPP
#define TUNNEL_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class TunnelChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL DIMENSIONS ---
    float length_meters;       // Longer tunnels = higher ventilation risk
    
    // --- DYNAMIC STATE ---
    float structural_health;   // 0.0 (Collapsed) -> 1.0 (Perfect)
    bool is_maintenance_active;// Regular closures for cleaning/inspection
    bool is_flooded;           // Binary blockage
    bool ventilation_active;   // If false, flow throttles significantly
    float seismic_rating;      // Resistance to shear forces
    
    // Constructor
    TunnelChokePoint(int id, std::string name, double lat, double lon, float length);

    // Calculates flow based on Health + Flood + Vents + Maintenance
    float calculate_throughput_modifier() override;

    // Listens for 'env_hazard', 'seismic', and 'maintenance'
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif