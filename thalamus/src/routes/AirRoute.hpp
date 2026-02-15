#ifndef AIR_ROUTE_HPP
#define AIR_ROUTE_HPP

#include "BaseRoute.hpp"

class AirRoute : public BaseRoute {
public:
    // Static 
    float distance_nm;                  // Great Circle Distance (Nautical Miles)
    int min_flight_level;               // Minimum Enroute Altitude (MEA)
    int max_flight_level;               // Service Ceiling / Airspace Cap
    bool etops_required;                // Requires extended twin-engine safety rating
    int etops_rating_minutes;           // Distance to nearest divert

    // Dynamic
    float wind_component_knots;         // Wind Speed (+/- knots)
    float turbulence_index;             // Turbulence Score (0-1)
    bool is_icing_conditions;           // Ice Conditions Flag
    
    // Security Flags
    bool is_conflict_zone;              // Conflict/Military Zones
    bool is_closed;                     // Closed Airspace

    // Calculated
    float effective_ground_speed_kts;   // TAS +/- Wind
    float estimated_fuel_burn_kg;       // Specific Fuel Consumption * Time

    AirRoute(long long id, std::string name);

    void parse_osm_tags();     
    void update_metrics();    
    
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};

#endif