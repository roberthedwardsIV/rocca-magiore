#ifndef RAIL_ROUTE_HPP
#define RAIL_ROUTE_HPP

#include "BaseRoute.hpp"

class RailRoute : public BaseRoute {
public:
    // Static
    float gauge_mm;                     // Distance between two rails (mm)
    bool is_electrified;                // Electrification
    float voltage_kv;                   // Track Voltage (kV - DC) 
    float max_axle_load_tons;           // Weight Limit (tons)
    float loading_gauge_height;         // Height Limit (m)
    float design_speed_kmh;             // Base Speed Limit (kmh)
    float overturning_wind_speed_kmh;   // Wind Speed Limit (kmh)

    // Dynamic
    int number_of_tracks;               // Track Number
    float signaling_headway_min;        // Headway Time Needed (min)
    float track_integrity;              // Track Health (0-1)
    bool power_active;                  // Catenary Status
    
    // Calculated
    float current_flow_trains_per_hour; // Current Capacity
    float effective_max_speed_kmh;      // True Speed Limit
    float travel_time_hours;            // Time of Travel (hours)

    RailRoute(long long id, std::string name);

    void parse_osm_tags();     
    void update_metrics();     
    
    void process_packet(const json& sig) override;
    json get_json_state() const override;

    private:
        float calculate_congestion_delay(float volume, float capacity) const;
};

#endif