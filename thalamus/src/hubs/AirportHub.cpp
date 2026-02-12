#include "AirportHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

AirportHub::AirportHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "airport", lat, lon) {
    
    // Default Infrastructure (Small Regional)
    number_of_runways = 1;
    max_runway_length_m = 1500.0f; // Turboprop / Private Jet only
    cargo_apron_spots = 2;         // Minimal cargo parking
    has_rail_access = false;

    // Default Capacity
    max_slots_per_hour = 10.0f;    // Low traffic
    cargo_terminal_area_sqm = 500.0f; 
    cold_storage_capacity_m3 = 0.0f; // No pharma capability

    // Default Identifiers
    iata_code = "UNK";
    icao_code = "ZZZZ";
    airport_type = "regional";

    // Operational State
    current_slot_utilization = 0.0f;
    current_parked_freighters = 0;
    is_curfew_active = false;
}

void AirportHub::parse_osm_tags() {
    // 1. Identifiers (IATA/ICAO)
    if (tags.count("iata")) iata_code = tags["iata"];
    if (tags.count("icao")) icao_code = tags["icao"];

    // 2. Aerodrome Type (The big heuristic driver)
    if (tags.count("aerodrome:type")) {
        airport_type = tags["aerodrome:type"];
    } else if (tags.count("aeroway")) {
        // e.g., aeroway=aerodrome
        airport_type = tags.count("type") ? tags["type"] : "public"; 
    }

    // 3. Heuristics based on Type
    if (airport_type == "international" || airport_type == "major") {
        // Massive Hub (e.g., Heathrow, JFK)
        number_of_runways = 2; // Conservative minimum for major hubs
        max_runway_length_m = 3500.0f; // Can land anything (A380/An-225)
        cargo_apron_spots = 20;
        max_slots_per_hour = 60.0f; 
        cargo_terminal_area_sqm = 50000.0f;
        cold_storage_capacity_m3 = 5000.0f; // Vaccine capable
        has_rail_access = true; // Likely
    } 
    else if (airport_type == "regional") {
        // Feeder Airport
        number_of_runways = 1;
        max_runway_length_m = 2000.0f; // 737/A320 capable
        cargo_apron_spots = 5;
        max_slots_per_hour = 20.0f;
    }
    else if (airport_type == "military") {
        // Airbase
        max_runway_length_m = 3000.0f; // Fast jets / heavy transport
        cargo_apron_spots = 10;
        is_curfew_active = false; // Military ignores curfew
    }

    // 4. Specific Overrides (If OSM has detailed tagging)
    if (tags.count("runway:length")) {
        try { max_runway_length_m = std::stof(tags["runway:length"]); } catch (...) {}
    }
    
    // 5. Cargo Specifics
    if (tags.count("cargo") && tags["cargo"] == "yes") {
        cargo_apron_spots += 10; // Boost cargo capacity
    }
}

void AirportHub::update_status() {
    // 1. Curfew Logic (Simple check - real implementation needs TimeManager)
    // Assume simulation passes a "time_of_day" but here we just flag it.
    // If it's night (23:00 - 06:00), curfew is active for most civil airports.
    // Logic placeholder:
    // if (TimeManager::is_night() && airport_type != "military") is_curfew_active = true;
    
    if (is_curfew_active) {
        max_slots_per_hour = 0.0f; // Airport closed to landings
    } else {
        // Reset to nominal capacity
        max_slots_per_hour = (airport_type == "international") ? 60.0f : 20.0f;
    }

    // 2. Slot Utilization Calculation
    // Utilization = (Scheduled Flights) / Max Capacity
    // If utilization > 1.0, delays stack up exponentially.
    // We don't have the live flight schedule here, so we default to 0.
    // This would be updated by the Simulation Engine pushing flight data.
}

bool AirportHub::can_accept_aircraft(const std::string& aircraft_type, float required_runway_m) const {
    // 1. Runway Length Check
    if (required_runway_m > max_runway_length_m) return false;

    // 2. Slot Availability Check
    if (current_slot_utilization >= 1.0f) return false; // Holding pattern required

    // 3. Parking Check
    if (current_parked_freighters >= cargo_apron_spots) return false; // No stand available

    return true;
}