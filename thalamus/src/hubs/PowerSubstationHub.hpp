#ifndef POWER_SUBSTATION_HUB_HPP
#define POWER_SUBSTATION_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class PowerSubstationHub : public BaseHub {
public:
    // --- ELECTRICAL INFRASTRUCTURE (Static) ---
    float high_voltage_kv;      // Primary side (e.g., 380kV)
    float low_voltage_kv;       // Secondary side (e.g., 110kV)
    int transformer_count;      // Redundancy (N-1 criterion)
    bool is_hvdc_converter;     // Converter Station (AC/DC)
    
    // --- CAPACITY METRICS (Physics Limits) ---
    float total_capacity_mva;   // Max Apparent Power (Hard Limit)
    float firm_capacity_mva;    // N-1 Limit (Capacity if one unit fails)

    // --- DYNAMIC STATE ---
    float current_load_mw;      // Real Power
    float current_load_mvar;    // Reactive Power
    float power_factor;         // Efficiency (0.0 - 1.0)
    
    // --- THERMAL PHYSICS ---
    float transformer_temp_c;   // Core temperature
    float ambient_temp_c;       // Weather input (affects cooling)
    
    // --- OPERATIONAL STATE ---
    bool is_tripped;            // Protection relay status (Blackout)
    bool is_maintenance;        // Scheduled downtime
    
    // --- IDENTIFIERS ---
    // "transmission", "distribution", "traction" (Rail), "industrial"
    std::string substation_type;

    // Constructor
    PowerSubstationHub(long long id, std::string name, double lat, double lon);

    // --- INTERFACE IMPLEMENTATION ---
    // Parses "voltage", "rating", "transformers"
    void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) override; 
    
    // Calculates Temp Rise ($I^2R$) and Trip Logic
    void update_metrics();

    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;

    // Helper: N-1 Contingency Check
    bool has_n_minus_1_security() const;
};

#endif