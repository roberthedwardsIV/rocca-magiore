#ifndef AIR_ROUTE_HPP
#define AIR_ROUTE_HPP

#include "BaseRoute.hpp"

class AirRoute : public BaseRoute {
public:
    // --- PHYSICAL GEOMETRY ---
    float distance_nm;         // Nautical Miles (Standard for Aviation)
    int min_flight_level;      // Minimum altitude (Mountain clearance / MEA)
    int max_flight_level;      // Maximum altitude (Airspace ceiling)
    
    // --- DYNAMIC WEATHER (The "Jet Stream") ---
    float wind_component_knots;// Positive = Tailwind, Negative = Headwind
    float turbulence_index;    // 0.0 (Smooth) -> 1.0 (Severe / Divert)
    bool is_icing_conditions;  // Affects lower altitudes

    // --- REGULATORY & RISK ---
    bool etops_required;       // True if >60 min from an airport (Ocean crossing)
    int etops_rating_minutes;  // e.g., 120, 180 (Required rating to fly this route)
    bool is_conflict_zone;     // War zone / Missile threat (MH17 scenario)
    bool is_closed;            // Complete airspace closure (Volcanic Ash / Politics)

    // --- OPERATIONAL METRICS ---
    float effective_ground_speed_kts; // True Airspeed +/- Wind
    float estimated_fuel_burn_kg;     // Burn varies massively with wind

    // Constructor
    AirRoute(long long id, std::string name);

    // Parses "airway", "ref", "min_altitude"
    void parse_osm_tags();

    // Calculates ground speed based on cruise speed (Mach 0.85) vs Wind
    void update_metrics();
};

#endif