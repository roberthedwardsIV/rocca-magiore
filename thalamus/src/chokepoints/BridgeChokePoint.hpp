#ifndef BRIDGE_CHOKEPOINT_HPP
#define BRIDGE_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class BridgeChokePoint : public BaseChokePoint {
public:
    // --- GEOMETRY (Static) ---
    float span_length_m;       // Main span length (determines stiffness)
    float deck_width_m;        // Aerodynamic chord length (B)
    float clearance_height_m;  // Height above terrain (z)
    float max_load_tons;       // Live load capacity

    // --- STRUCTURAL PHYSICS (Derived) ---
    float natural_freq_hz;         // Fundamental torsional frequency
    float critical_flutter_vel_ms; // Selberg Speed ($U_{cr}$) - Structural Failure Limit
    float vehicle_overturn_vel_ms; // Lateral Force Limit - Traffic Safety Limit

    // --- DYNAMIC STATE ---
    float current_wind_speed_ms;   // Local wind velocity ($U$)
    float air_density_kgm3;        // $\rho$ (varies with temp/altitude)
    float structural_health;       // 0.0 - 1.0 (Stiffness degradation)
    bool is_maintenance;

    // Constructor
    BridgeChokePoint(int id, std::string name, double lat, double lon, 
                     float span, float width, float height);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Returns 0.0 if $U > U_{cr}$ (Collapse Risk) or $U > U_{tip}$ (Traffic Ban)
    // Returns <1.0 based on structural health degradation
    float calculate_throughput_modifier() override;

    // Listens for 'weather' (wind/temp) and 'seismic' (health)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    // Recalculates limits when Health or Geometry changes
    void update_physics_limits();
};

#endif