#ifndef RAIL_NODE_HUB_HPP
#define RAIL_NODE_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class RailNodeHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE (Static) ---
    int number_of_tracks;       // Classification tracks
    float max_train_length_m;   // Longest receiving track (limits train size)
    bool is_hump_yard;          // True = Gravity sorting (Continuous flow)
    bool is_electrified;        // Catenary status
    bool has_intermodal_lift;   // Container Gantry availability

    // --- KINEMATICS & EQUIPMENT (Physics) ---
    float hump_speed_kmh;       // Gravity sorting speed
    float switcher_accel_ms2;   // Locomotive acceleration (flat switching)
    float avg_wagon_length_m;   // Standard car length
    
    // --- CAPACITY STATE (Queuing Theory) ---
    int max_storage_wagons;     // Geometric limit (Total Track Length / Wagon Length)
    int current_wagons_stored;  // Inventory (Queue Length)
    
    // --- OPERATIONAL METRICS (Calculated) ---
    float service_rate_wagons_hr; // μ (Processing power)
    float arrival_rate_wagons_hr; // λ (Incoming flow)
    float utilization_rho;        // ρ = λ / μ
    float average_dwell_time_h;   // E(W) from Kingman's Formula

    // --- IDENTIFIERS ---
    std::string yard_type;      // "marshalling", "intermodal", "industrial"

    // Constructor
    RailNodeHub(long long id, std::string name, double lat, double lon);

    // --- INTERFACE IMPLEMENTATION ---
    void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) override; 
    
    // Updates dwell time using Kingman's Approximation:
    // Delay explodes exponentially as utilization approaches 1.0
    void update_metrics();

    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;

    // Helper: Geometry check
    bool can_accept_train(float train_length_m) const;
};

#endif