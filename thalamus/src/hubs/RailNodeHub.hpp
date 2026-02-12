#ifndef RAIL_NODE_HUB_HPP
#define RAIL_NODE_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class RailNodeHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE ---
    int number_of_tracks;       // Total tracks in the yard
    float max_train_length_m;   // Longest siding (limiting factor for train size)
    bool is_hump_yard;          // True = Gravity sorting (High throughput)
    bool is_electrified;        // Can electric locos enter?
    bool has_intermodal_lift;   // Can transfer containers to trucks (Gantry cranes)

    // --- CAPACITY METRICS ---
    int classification_bowl_capacity; // Max wagons/cars in sorting area
    int current_wagons_stored;        // Current inventory
    float processing_speed_wagons_hr; // Sorting rate (Cars per hour)

    // --- INTERMODAL STATE ---
    int container_lifts_per_hour;     // Crane speed (if intermodal)
    int truck_gate_lanes;             // For drayage trucks

    // --- OPERATIONAL STATE ---
    float congestion_level;           // 0.0 - 1.0 (Yard utilization)
    float average_dwell_time_h;       // Time a wagon spends in the yard

    // --- IDENTIFIERS ---
    std::string yard_type;      // "marshalling", "intermodal", "station", "industrial"
    std::string operator_name;  // e.g., "DB Cargo", "Union Pacific"

    // Constructor
    RailNodeHub(long long id, std::string name, double lat, double lon);

    // Parses "railway=yard", "service=siding", "electrified"
    void parse_osm_tags() override;

    // Updates throughput based on Hump Yard status and Congestion
    // Calculates dwell time penalty if yard is full
    void update_status() override;
    
    // Returns true if a specific train length fits in the receiving tracks
    bool can_accept_train(float train_length_m) const;
};

#endif