#ifndef CANAL_LOCK_CHOKEPOINT_HPP
#define CANAL_LOCK_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class CanalLockChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL CONSTRAINTS ---
    float max_draft_meters;    // Determines if a specific ship fits
    float max_beam_meters;     // Width constraint (Panamax vs. NeoPanamax)
    
    // --- DYNAMIC STATE ---
    float water_level;         // 0.0 (Dry/Drought) -> 1.0 (Optimal)
    float mechanical_health;   // 0.0 (Broken Gates) -> 1.0 (Functional)
    float queue_size;          // Number of ships waiting (Congestion)
    
    // Constructor
    CanalLockChokePoint(int id, std::string name, double lat, double lon, 
                        float max_draft, float max_beam);

    // Calculates flow based on Water Levels + Mechanics
    float calculate_throughput_modifier() override;

    // Listens for 'environmental' (drought) and 'mechanical' signals
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif