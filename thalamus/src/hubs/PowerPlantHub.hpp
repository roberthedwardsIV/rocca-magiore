#ifndef POWER_PLANT_HUB_HPP
#define POWER_PLANT_HUB_HPP

#include "BaseHub.hpp"
#include <string>

class PowerPlantHub : public BaseHub {
public:
    // --- GENERATION CAPABILITY (Static) ---
    float max_output_mw;        // Nameplate Capacity
    float min_stable_load_mw;   // Minimum run level (Coal/Nuclear)
    float ramp_rate_mw_min;     // Physical change limit (Thermal Stress)
    bool is_dispatchable;       // False for Solar/Wind (Weather dependent)
    
    // --- FUEL & EFFICIENCY (Physics) ---
    std::string fuel_source;    // "coal", "gas", "nuclear", "solar", "wind"
    std::string gen_technology; // "ccgt", "ocgt", "pwr", "pv"
    float base_heat_rate;       // Efficiency metric (MMBtu/MWh) - Lower is better
    
    // --- DYNAMIC STATE ---
    float current_output_mw;    // Real-time generation
    float target_output_mw;     // Dispatch instruction
    float fuel_inventory;       // Tons (Coal) or Pressure/Unit (Gas)
    
    // --- WEATHER INPUTS (For Renewables) ---
    float wind_speed_ms;        // Local wind
    float solar_irradiance;     // W/m2
    
    // --- OPERATIONAL METRICS (Calculated) ---
    float capacity_factor;      // 0.0 - 1.0
    float fuel_burn_rate;       // Units per hour
    bool is_tripped;            // Safety shutdown

    // Constructor
    PowerPlantHub(long long id, std::string name, double lat, double lon);

    // --- INTERFACE IMPLEMENTATION ---
    // Parses "generator:source", "plant:output:electricity", "generator:method"
    void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) override; 
    
    // Calculates Output based on Ramp Limits, Weather (Renewables), or Fuel (Thermal)
    void update_metrics();

    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;

    // Helper: Receive dispatch instruction
    void set_dispatch_target(float mw);
};

#endif