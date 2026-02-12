#ifndef MARITIME_ROUTE_HPP
#define MARITIME_ROUTE_HPP

#include "BaseRoute.hpp"

class MaritimeRoute : public BaseRoute {
public:
    // --- PHYSICAL CONSTRAINTS ---
    float max_draft_meters;       // The "Depth Limit" (e.g., 20m Malacca Strait)
    bool requires_ice_breaker;    // Northern Sea Route / Arctic
    float distance_nautical_miles;// Length in NM (1 NM = 1.852 km)

    // --- DYNAMIC CONDITIONS ---
    float sea_state_level;        // Douglas Scale: 0 (Glass) -> 9 (Phenomenal)
    float wind_speed_knots;       // Headwinds slow vessels down massively
    
    // --- SECURITY & RISK ---
    // 0=Safe, 1=Low, 2=Elevated, 3=High, 4=Severe, 5=Critical (No-Go)
    int threat_level;             
    std::string threat_type;      // "piracy", "war_zone", "naval_blockade", "missile_range"
    float insurance_premium_mult; // War Risk Surcharge (e.g., 10x cost)

    // --- OPERATIONAL METRICS ---
    float effective_speed_knots;  // Real speed after weather/threat penalties
    float travel_time_hours;      // Latency

    // --- OSM ATTRIBUTES ---
    std::string zone_type;        // "eez", "international", "contiguous"
    std::string seamark_type;     // "fairway", "separation_lane"

    // Constructor
    MaritimeRoute(long long id, std::string name);

    // Parses "seamark:type", "depth", "width"
    void parse_osm_tags();

    // Calculates effective speed based on Sea State + Evasive Maneuvers (Threats)
    void update_metrics();
};

#endif