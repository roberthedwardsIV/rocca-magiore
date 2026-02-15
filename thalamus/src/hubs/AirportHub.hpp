#ifndef AIRPORT_HUB_HPP
#define AIRPORT_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class AirportHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE (Static) ---
    int number_of_runways;
    float max_runway_length_m;  // Limits aircraft size (Takeoff distance)
    float cargo_apron_area_sqm; // Total paved area for freighters
    bool has_rail_access;       // Critical for multimodal logistics

    // --- PHYSICS PARAMETERS ---
    float avg_runway_occupancy_time_s; // Time required to land & exit
    float wake_turbulence_separation_nm; // Spacing required between heavy jets
    float approach_speed_knots;        // Avg landing speed

    // --- CAPACITY STATE ---
    int max_parking_spots;      // Derived from Apron Area / Freighter Footprint
    int current_parked_freighters;
    
    // --- COLD CHAIN ---
    float cold_storage_volume_m3;
    float current_cold_storage_used;

    // --- OPERATIONAL METRICS (Calculated) ---
    float max_slots_per_hour;   // Theoretical throughput
    float current_slot_utilization; // 0.0 - 1.0
    bool is_curfew_active;      // Noise restrictions

    // --- IDENTIFIERS ---
    std::string iata_code;
    std::string icao_code;
    std::string airport_type;   // "international", "regional", "military"

    // Constructor
    AirportHub(long long id, std::string name, double lat, double lon);

    // --- INTERFACE IMPLEMENTATION ---
    void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) override;
    
    // Calculates max slots based on wake separation physics
    void update_metrics();

    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;

    // Helper: Performance check
    bool can_accept_aircraft(const std::string& type_class, float required_runway_m) const;
};

#endif