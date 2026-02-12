#ifndef BORDER_CHOKEPOINT_HPP
#define BORDER_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class BorderChokePoint : public BaseChokePoint {
public:
    // --- STATIC DATA ---
    std::string country_a;
    std::string country_b;

    // --- DYNAMIC STATE ---
    float political_friction;      // 0.0 (Free Trade) -> 1.0 (Hostile/Embargo)
    float customs_delay_hours;     // Processing latency (The real killer for logistics)
    bool is_border_closed;         // Binary Override (War/Pandemic/Strike)

    // Constructor
    BorderChokePoint(int id, std::string name, double lat, double lon, 
                     std::string c1, std::string c2);

    // Calculates flow based on Politics (soft barrier) + Wait Times (hard delay)
    float calculate_throughput_modifier() override;

    // Listens for 'political' (sanctions) and 'logistics' (queue times)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif