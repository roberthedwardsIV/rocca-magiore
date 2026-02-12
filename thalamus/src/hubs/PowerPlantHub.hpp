#ifndef POWER_PLANT_HUB_HPP
#define POWER_PLANT_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class PowerPlantHub : public BaseHub {
public:
    // --- GENERATION CAPABILITY ---
    float max_output_mw;        // Nameplate Capacity (The hard limit)
    float min_stable_load_mw;   // Minimum output before shutting down (Coal/Nuclear)
    float ramp_rate_mw_min;     // How fast can it change output? (Gas=Fast, Nuclear=Slow)
    
    // --- FUEL & PHYSICS ---
    std::string fuel_source;    // "coal", "gas", "nuclear", "solar", "wind", "hydro"
    float efficiency_percent;   // Thermal efficiency (33% Coal vs 60% CCGT)
    bool is_dispatchable;       // Can we command it? (False for Solar/Wind)
    
    // --- DYNAMIC STATE ---
    float current_output_mw;    // Real-time generation
    float fuel_inventory_tons;  // Coal pile / Gas pressure (0 for Renewables)
    float capacity_factor;      // Current availability (0.0 - 1.0) due to maintenance/sun

    // --- OPERATIONAL ---
    bool is_peaker;             // Designed for short bursts (High cost, fast ramp)
    float startup_cost;         // Cost to turn on (discourages cycling)

    // Constructor
    PowerPlantHub(long long id, std::string name, double lat, double lon);

    // Parses "generator:source", "plant:output:electricity", "power=plant"
    // Estimates MW based on "area" for Solar/Wind if explicit tags missing.
    void parse_osm_tags() override;

    // Updates output based on Ramp Rate limits and Fuel Availability
    // For Renewables, this needs a "Weather Factor" input (simulated)
    void update_status() override;
    
    // Commands the plant to target a specific MW output (clamped by Ramp Rate)
    void set_target_output(float target_mw);
};

#endif