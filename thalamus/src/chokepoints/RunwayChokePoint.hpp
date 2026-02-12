#ifndef RUNWAY_CHOKEPOINT_HPP
#define RUNWAY_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class RunwayChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL DIMENSIONS ---
    float length_meters;       // Determines max takeoff weight for heavy freighters
    float width_meters;
    std::string surface_type;  // "asphalt", "concrete", "grass", "gravel"
    
    // --- DYNAMIC STATE ---
    float visibility_meters;   // Fog/Snow limit (METAR RVR)
    float friction_coefficient;// Braking action (0.0 = Ice, 0.8 = Dry)
    float crosswind_speed_kt;  // Crosswind component
    bool is_obstructed;        // Crash, debris, or stalled aircraft
    bool is_maintenance;       // Scheduled resurfacing

    // Constructor
    RunwayChokePoint(int id, std::string name, double lat, double lon, 
                     float length, float width, std::string surface);

    // Calculates flow based on Visibility + Friction + Crosswind
    float calculate_throughput_modifier() override;

    // Listens for 'weather' (METAR data) and 'obstruction'
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif