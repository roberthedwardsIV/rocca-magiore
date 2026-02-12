#ifndef AIRPORT_HUB_HPP
#define AIRPORT_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class AirportHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE ---
    int number_of_runways;
    float max_runway_length_m;  // Limits aircraft size (e.g., 747 needs ~3000m)
    int cargo_apron_spots;      // Parking spots for dedicated freighters
    bool has_rail_access;       // Rare, but exists for major hubs (e.g., Frankfurt)

    // --- CAPACITY METRICS ---
    float max_slots_per_hour;   // ATC limit (Takeoffs + Landings)
    float cargo_terminal_area_sqm; // Warehouse size
    float cold_storage_capacity_m3;// Critical for pharma/perishables

    // --- OPERATIONAL STATE ---
    float current_slot_utilization; // 0.0 - 1.0 (Traffic congestion)
    int current_parked_freighters;
    bool is_curfew_active;      // Night restrictions (stops cargo flights 23:00-06:00)

    // --- IDENTIFIERS ---
    std::string iata_code;      // "JFK", "LHR"
    std::string icao_code;      // "KJFK", "EGLL"
    std::string airport_type;   // "international", "regional", "military"

    // Constructor
    AirportHub(long long id, std::string name, double lat, double lon);

    // Parses "aerodrome:type", "iata", "runway:length"
    void parse_osm_tags() override;

    // Updates slot utilization and checks for curfew
    void update_status() override;

    // Returns true if a specific aircraft type (e.g., "B747-8F") can operate here
    bool can_accept_aircraft(const std::string& aircraft_type, float required_runway_m) const;
};

#endif