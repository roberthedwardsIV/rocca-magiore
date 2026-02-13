#ifndef ROAD_ROUTE_HPP
#define ROAD_ROUTE_HPP

#include "BaseRoute.hpp"

class RoadRoute : public BaseRoute {
public:
    // Static
    int lanes;                  // Lane count   
    int max_speed_kmh;          // Speed limit (km/hr)
    bool is_one_way;            // One-Way Road Flag
    float max_weight_tons;      // Weight limit (tons)
    std::string classification; // Road type

    // Dynamic
    float current_flow_vph;     // Curent vehicles per hour   
    float surface_quality;      // Integrity of road (0-1)
    float base_capacity_vph;    // Baseline vehicles per hour
    
    // Physics
    float base_friction;        // Baseline Friction Coefficient
    float current_friction;     // Baseline Friction Coefficient
    float heavy_vehicle_pct;    // Commercial Truck Percentage (%)

    // Calculated
    float travel_time_hours;    // Travel time (hours)
    float effective_speed_kmh;  // Current "Speed Limit" (kmh) 
    float current_weight_limit; // Current "Weight Limit" (tons)
    bool logistics_accessible;  // Flag for commercial truck navigability

    RoadRoute(long long id, std::string name);

    void parse_osm_tags();     
    void update_metrics();     
    
    void process_packet(const json& sig) override;
    json get_json_state() const override;

private:
    float calculate_bpr_delay(float volume, float capacity) const;
};

#endif