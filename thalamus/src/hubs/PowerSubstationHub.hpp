#ifndef POWER_SUBSTATION_HPP
#define POWER_SUBSTATION_HPP

#include "BaseHub.hpp"
#include <vector>

class PowerSubstation : public BaseHub {
public:
    // --- ELECTRICAL INFRASTRUCTURE ---
    float high_voltage_kv;      // Primary side (e.g., 380kV)
    float low_voltage_kv;       // Secondary side (e.g., 110kV)
    int transformer_count;      // Redundancy (N-1 criterion)
    
    // --- CAPACITY METRICS ---
    float total_capacity_mva;   // Max Apparent Power (The hard limit)
    float current_load_mw;      // Real Power flow
    float power_factor;         // Efficiency (usually 0.9 - 1.0)

    // --- OPERATIONAL STATE ---
    bool is_tripped;            // Protection relay status (True = Blackout)
    float transformer_temp_c;   // Core temperature (affects aging/risk)
    
    // --- TYPE SPECIFICS ---
    // "transmission", "distribution", "traction" (Rail), "converter" (HVDC)
    std::string substation_type;

    // Constructor
    PowerSubstation(long long id, std::string name, double lat, double lon);

    // Parses "voltage", "rating", "power=substation"
    // Heuristically estimates MVA if explicit ratings are missing.
    void parse_osm_tags() override;

    // Updates transformer temperature based on load
    // Trips the station if Load > Capacity for too long
    void update_status() override;
    
    // Returns true if the station can handle an additional load increment
    bool has_spare_capacity(float additional_load_mw) const;
};

#endif