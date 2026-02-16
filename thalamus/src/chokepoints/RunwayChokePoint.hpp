#ifndef RUNWAY_CHOKEPOINT_HPP
#define RUNWAY_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class RunwayChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL DIMENSIONS (Static) ---
    float length_meters;       // Takeoff Run Available (TORA)
    float width_meters;
    std::string surface_type;  // "asphalt", "concrete"

    // --- DYNAMIC STATE ---
    float visibility_meters;   // RVR (Runway Visual Range)
    float friction_coefficient;// Mu (0.0 - 1.0)
    float crosswind_speed_kt;  // Lateral wind component
    float headwind_speed_kt;   // Longitudinal wind component (+Head, -Tail)
    
    // --- OPERATIONAL FLAGS ---
    bool is_obstructed;        // FOD / Disabled Aircraft
    bool is_maintenance;       // Resurfacing

    // --- CALCULATED METRICS ---
    float current_capacity_ph; // Movements per hour (Arrivals + Departures)
    float design_capacity_ph;  // Max capacity in VMC (Visual Meteorological Conditions)
    float dynamic_crosswind_limit_kt; // Calculated limit based on friction

    // Constructor
    RunwayChokePoint(int id, std::string name, double lat, double lon, 
                     float length, float width, std::string surface);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates flow modifier as (Current Capacity / Design Capacity)
    // Returns 0.0 if crosswind > limit or tailwind > limit
    float calculate_throughput_modifier() override;

    // Listens for 'weather' (METAR) and 'obstruction'
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    // Recalculates limits based on friction and visibility rules
    void update_aero_physics();
};

#endif