#ifndef WATERWAY_ROUTE_HPP
#define WATERWAY_ROUTE_HPP

#include "BaseRoute.hpp"

class WaterwayRoute : public BaseRoute {
public:
    // --- PHYSICAL CONSTRAINTS (Static) ---
    float max_draft_meters;        // Channel depth (limiting factor)
    float max_air_draft_meters;    // Bridge clearance (limits container stacks)
    float channel_width_meters;    // Limits two-way traffic for wide barges
    int cemt_class;                // European standard (I-VII) determining vessel size
    int lock_count;                // Number of locks on this segment

    // --- DYNAMIC CONDITIONS (Hydrology) ---
    float current_speed_knots;     // +Downstream, -Upstream
    float water_level_offset_m;    // Flood (+) or Drought (-) affecting draft
    bool is_frozen;                // Winter blockage
    
    // --- OPERATIONAL METRICS (Calculated) ---
    float effective_draft_meters;  // Actual usable depth right now
    float max_deadweight_tons;     // Max cargo capacity per barge
    float effective_sog_knots;     // Speed Over Ground (Vessel +/- Current)
    float lock_penalty_hours;      // Total time lost to locking
    float travel_time_hours;       // (Dist / SOG) + Lock Penalty

    // Constructor
    WaterwayRoute(long long id, std::string name);

    // --- INTERFACE IMPLEMENTATION ---
    void parse_osm_tags();     // Parses CEMT, depth, bridges
    void update_metrics();     // Calculates SOG and Lock Delays
    
    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};

#endif