#ifndef RAIL_ROUTE_HPP
#define RAIL_ROUTE_HPP

#include "BaseRoute.hpp"

class RailRoute : public BaseRoute {
public:
    // --- PHYSICAL CONSTRAINTS (The "Hard" Limits) ---
    float gauge_mm;            // 1435 (Standard), 1520 (Russian), 1668 (Iberian), 1000 (Meter)
    bool is_electrified;       // If false, electric locos cannot pass (0 capacity for them)
    float voltage_kv;          // 25.0, 15.0, 3.0 DC (Incompatible voltages require loco change)
    float max_axle_load_tons;  // 22.5t is standard EU. 30t+ for heavy haul (US/Aus).
    float loading_gauge_height;// Max height of cargo (e.g., double-stack containers)

    // --- CAPACITY METRICS ---
    int number_of_tracks;      // 1 (Single) vs 2 (Double) vs 4 (Quad)
    float max_speed_kmh;       // Line speed limit
    float signaling_headway_min; // Minimum time between trains (e.g., 3 mins vs 15 mins)
    
    // --- DYNAMIC STATE ---
    float current_flow_trains_per_hour;
    bool is_main_line;         // "usage=main" vs "usage=branch/industrial"

    // Constructor
    RailRoute(long long id, std::string name);

    // Parses "gauge", "voltage", "frequency", "tracks", "usage"
    void parse_osm_tags();

    // Updates max capacity based on signaling and track count
    void update_metrics();
};

#endif