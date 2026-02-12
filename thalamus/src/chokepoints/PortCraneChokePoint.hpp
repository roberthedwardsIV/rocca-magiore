#ifndef PORT_CRANE_CHOKEPOINT_HPP
#define PORT_CRANE_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class PortCraneChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL CAPACITY ---
    float max_swl_tons;        // Safe Working Load (e.g. 65t for twin-lift)
    float outreach_meters;     // Reach (Panamax vs. Post-Panamax)
    float wind_limit_kmh;      // Safety cutoff (usually ~72 km/h)

    // --- DYNAMIC STATE ---
    float current_wind_speed;  // Local anemometer reading
    float hoist_efficiency;    // 1.0 (New) -> 0.5 (Aging/Slow)
    bool is_operational;       // Breakdown status
    bool is_in_use;            // Is it currently assigned to a ship?

    // Constructor
    PortCraneChokePoint(int id, std::string name, double lat, double lon, 
                        float swl, float outreach);

    // Calculates Lifting Capacity (0.0 = Stowed/Winded Off)
    float calculate_throughput_modifier() override;

    // Listens for 'weather' (wind) and 'mechanical' (breakdown)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif