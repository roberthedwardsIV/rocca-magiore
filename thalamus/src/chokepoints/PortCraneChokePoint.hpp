#ifndef PORT_CRANE_CHOKEPOINT_HPP
#define PORT_CRANE_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class PortCraneChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL DIMENSIONS (Static) ---
    float max_swl_tons;        // Safe Working Load (e.g., 65t)
    float outreach_meters;     // Boom length (determines wind moment)
    float hoist_speed_ms;      // Vertical speed
    float trolley_speed_ms;    // Horizontal speed

    // --- DYNAMIC STATE ---
    float current_wind_speed_kmh; // Anemometer reading
    float mechanical_health;      // 0.0 - 1.0 (Wear and tear)
    bool is_operational;          // Maintenance/Breakdown
    bool is_in_use;               // Active assignment

    // --- CALCULATED LIMITS ---
    float dynamic_wind_limit_kmh; // Calculated based on load aerodynamics
    float cycle_time_seconds;     // Theoretical move time

    // Constructor
    PortCraneChokePoint(int id, std::string name, double lat, double lon, 
                        float swl, float outreach);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates efficiency (0.0 - 1.0) based on Wind vs Limit and Health
    float calculate_throughput_modifier() override;

    // Listens for 'weather' (wind), 'mechanical' (health), 'ops' (usage)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    void update_physics_limits();
};

#endif