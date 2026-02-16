#ifndef BORDER_CHOKEPOINT_HPP
#define BORDER_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class BorderChokePoint : public BaseChokePoint {
public:
    // --- STATIC INFRASTRUCTURE ---
    int num_lanes;              // Physical lanes
    int num_inspection_bays;    // Secondary inspection capacity
    std::string border_type;    // "schengen", "hard", "hostile"
    
    // --- IDENTIFIERS ---
    std::string country_a;      // e.g., "USA"
    std::string country_b;      // e.g., "CAN"

    // --- GEOPOLITICAL STATE ---
    float political_friction;   // 0.0 (Open) -> 1.0 (Embargo/War)
    float inspection_rate_pct;  // % of vehicles pulled for scan (Derived from Friction)
    bool is_closed;             // Binary blockade

    // --- OPERATIONAL METRICS (Queuing) ---
    float traffic_volume_vph;   // Vehicles per hour (Arrival Rate λ)
    float avg_process_time_m;   // Minutes per vehicle (Service Time 1/μ)
    float effective_capacity_vph; // μ (Service Rate)
    
    // --- CALCULATED ---
    float estimated_wait_time_h; // Queuing Delay

    // Constructor
    BorderChokePoint(int id, std::string name, double lat, double lon, 
                     int lanes, std::string type, 
                     std::string c1, std::string c2);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates flow modifier based on Delay vs. Logistics Tolerance
    float calculate_throughput_modifier() override;

    // Listens for 'politics' (friction) and 'traffic' (volume)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    void update_processing_physics();
};

#endif