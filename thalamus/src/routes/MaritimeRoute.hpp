#ifndef MARITIME_ROUTE_HPP
#define MARITIME_ROUTE_HPP

#include "BaseRoute.hpp"

class MaritimeRoute : public BaseRoute {
public:
    // Static
    float max_draft_meters;        // Depth Limit (m)
    bool requires_ice_breaker;     // Flag for iced routes
    float distance_nautical_miles; // Length in NM (1 NM = 1.852 km)
    float design_speed_knots;      // Theoretical speed of vessel class
    std::string zone_type;         // "eez", "international", "contiguous"
    std::string seamark_type;      // "fairway", "separation_lane"

    // Dynamic
    float sea_state_level;         // Douglas Scale (0-9)
    float wave_height_meters;      // Swell/wind wave height (m)
    float wind_component_knots;    // Wind speed (+/- knots)
    float ice_coverage_pct;        // Coverage of ice (%)
    
    // Security + Risk
    int threat_level;              // Defcom level (0-5)
    std::string threat_type;       // Threat type (string)
    float insurance_premium_mult;  // War Risk Surcharge

    // Calculated
    float effective_speed_knots;   // Current Speed-Over-Ground (knots)
    float travel_time_hours;       // Latency (Distance / SOG)
    float fuel_efficiency_multiplier; // Fuel burn penalty factor

    // Constructor
    MaritimeRoute(long long id, std::string name);

    void parse_osm_tags();     
    void update_metrics();     
    
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};

#endif