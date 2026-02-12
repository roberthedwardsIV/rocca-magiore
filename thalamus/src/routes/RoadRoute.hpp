#ifndef ROAD_ROUTE_HPP
#define ROAD_ROUTE_HPP

#include "BaseRoute.hpp"

class RoadRoute : public BaseRoute {
public:
    // --- QUANTITATIVE METRICS (For Physics & Triggers) ---
    float base_capacity_vph;   // Vehicles Per Hour (e.g., 2000 * lanes)
    float current_flow_vph;    // Real-time traffic throughput
    float max_weight_tons;     // Critical for heavy logistics (bridge/road limits)
    float surface_quality;     // 0.0 (Destroyed) -> 1.0 (Perfect Asphalt)
    float travel_time_hours;   // Dynamic latency based on length/speed/traffic
    float length_km;           // Cached length for speed calculations

    // --- OSM ATTRIBUTES ---
    int lanes;                 // "lanes" tag
    int max_speed_kmh;         // "maxspeed" tag
    bool is_one_way;           // "oneway" tag
    std::string classification;// "highway" tag (motorway vs residential)

    // Constructor
    RoadRoute(long long id, std::string name);

    // Parses tags to calculate the quantitative baselines above
    // e.g., surface="dirt" -> surface_quality = 0.4
    void parse_osm_tags();

    // Updates travel_time_hours based on current flow and surface quality
    void update_metrics();
};

#endif