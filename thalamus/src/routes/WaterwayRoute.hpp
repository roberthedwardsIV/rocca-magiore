#ifndef WATERWAY_ROUTE_HPP
#define WATERWAY_ROUTE_HPP

#include "BaseRoute.hpp"

class WaterwayRoute : public BaseRoute {
public:
    // --- PHYSICAL GEOMETRY ---
    float max_draft_meters;    // Depth limit (critical for barge loading)
    float air_draft_meters;    // Bridge clearance (critical for container stacking)
    float beam_meters;         // Lock width limit
    std::string cemt_class;    // European Standard (I - VII)
    
    // --- HYDROLOGY (Dynamic) ---
    float current_speed_kmh;   // Flow rate (positive = downstream)
    float water_level_stage;   // Deviation from normal (e.g., -1.5m drought)
    bool is_frozen;            // Winter stoppage

    // --- NAVIGABILITY & RISK ---
    // 0=Open, 1=Restricted (Light loading only), 2=One-Way, 3=Closed
    int navigability_status;   
    
    // 0=Safe, ... 5=Conflict Zone / Dam Failure Risk
    int threat_level;          
    std::string hazard_type;   // "drought", "ice", "debris", "civil_unrest", "maintenance"

    // --- OPERATIONAL ---
    bool is_canal;             // True = Still water, False = Flowing River
    float upstream_speed_kmh;  // Effective speed fighting current
    float downstream_speed_kmh;// Effective speed with current

    // Constructor
    WaterwayRoute(long long id, std::string name);

    // Parses "CEMT", "waterway=river", "maxdraft", "seamark:bridge:clearance"
    void parse_osm_tags();

    // Calculates speeds based on current vs engine power
    void update_metrics();
};

#endif