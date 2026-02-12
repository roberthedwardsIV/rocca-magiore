#ifndef DAM_CHOKEPOINT_HPP
#define DAM_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class DamChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL CAPACITY ---
    float max_capacity_m3;     // Total reservoir volume
    float spillway_limit_m3s;  // Max flow rate it can safely release

    // --- DYNAMIC STATE ---
    float current_level_pct;   // 0.0 (Empty) -> 1.0 (Full/Overtopping)
    float structural_health;   // 0.0 (Breached) -> 1.0 (Solid)
    bool is_generating_power;  // If true, water must flow through turbines

    // Constructor
    DamChokePoint(int id, std::string name, double lat, double lon, float capacity);

    // Calculates downstream flow modifier based on Level + Health
    // (A dam can choke a river by holding water BACK, or destroy it by failing)
    float calculate_throughput_modifier() override;

    // Listens for 'water_level' (satellite altimetry) and 'integrity'
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif