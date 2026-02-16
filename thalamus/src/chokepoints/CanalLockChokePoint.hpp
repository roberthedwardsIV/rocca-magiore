#ifndef CANAL_LOCK_CHOKEPOINT_HPP
#define CANAL_LOCK_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class CanalLockChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL GEOMETRY (Static) ---
    float chamber_length_m;
    float chamber_width_m;
    float sill_depth_m;        // Maximum depth at chart datum
    float lift_height_m;       // Vertical distance to lift/lower
    float design_cycle_time_m; // Nominal time (Entry + Fill + Exit)

    // --- DYNAMIC STATE ---
    float water_level_offset_m; // Drought (-) or Flood (+) relative to datum
    float fill_rate_m3s;        // Hydraulic pump/gravity flow rate
    bool is_maintenance;        // Mechanical status

    // --- CALCULATED METRICS ---
    float current_usable_depth_m; // Depth - Safety Margin
    float max_passable_tonnage;   // Deadweight tonnage of largest allowed ship
    float design_max_tonnage;     // Deadweight tonnage of design ship

    // Constructor
    CanalLockChokePoint(int id, std::string name, double lat, double lon, 
                        float length, float width, float depth);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates flow as (Current Max Tonnage / Design Max Tonnage) * Cycle Efficiency
    // 0.0 if blocked, <1.0 if drought forces light-loading or smaller ships
    float calculate_throughput_modifier() override;

    // Listens for 'hydrology' (water levels) and 'maintenance'
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    // Helper to estimate DWT from dimensions (Block Coefficient method)
    float estimate_max_dwt(float draft_limit) const;
};

#endif