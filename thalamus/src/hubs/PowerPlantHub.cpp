#include "PowerPlantHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

// Internal state tracking for dispatch instructions
float target_output_mw = 0.0f;

PowerPlantHub::PowerPlantHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "power_plant", lat, lon) {
    
    // Default Generation (Small Gas Peaker)
    max_output_mw = 50.0f;      
    min_stable_load_mw = 10.0f; 
    ramp_rate_mw_min = 5.0f;    // Fast (5 MW/min)
    
    // Default Physics
    fuel_source = "gas";
    efficiency_percent = 0.35f; // Standard OCGT
    is_dispatchable = true;     // We control it

    // Default State
    current_output_mw = 0.0f;
    fuel_inventory_tons = 1000.0f; // Gas pressure/storage
    capacity_factor = 1.0f;     // Fully available

    // Operational defaults
    is_peaker = true;
    startup_cost = 5000.0f;     // Expensive to start
    
    target_output_mw = 0.0f;
}

void PowerPlantHub::parse_osm_tags() {
    // 1. Fuel Source Detection
    if (tags.count("generator:source")) {
        fuel_source = tags["generator:source"];
    } else if (tags.count("power_source")) {
        fuel_source = tags["power_source"]; // Older tag
    }

    // 2. Physics & Ramp Rate Assignment based on Fuel
    if (fuel_source == "nuclear") {
        is_dispatchable = true;
        is_peaker = false;
        min_stable_load_mw = max_output_mw * 0.5f; // Cannot run below 50%
        ramp_rate_mw_min = max_output_mw * 0.01f;  // Very slow ramp (1%/min)
        efficiency_percent = 0.33f;
    } 
    else if (fuel_source == "coal") {
        is_dispatchable = true;
        is_peaker = false;
        min_stable_load_mw = max_output_mw * 0.3f;
        ramp_rate_mw_min = max_output_mw * 0.02f;  // Slow ramp
        efficiency_percent = 0.38f;
    }
    else if (fuel_source == "gas") {
        is_dispatchable = true;
        // Check method: CCGT (Combined Cycle) vs OCGT (Peaker)
        if (tags.count("generator:method") && tags["generator:method"] == "combined_cycle") {
            is_peaker = false;
            efficiency_percent = 0.60f; // High efficiency
            ramp_rate_mw_min = max_output_mw * 0.05f;
        } else {
            is_peaker = true; // Gas Turbine
            efficiency_percent = 0.35f;
            ramp_rate_mw_min = max_output_mw * 0.20f; // Fast ramp (20%/min)
        }
    }
    else if (fuel_source == "solar") {
        is_dispatchable = false; // Weather dependent
        min_stable_load_mw = 0.0f;
        ramp_rate_mw_min = 9999.0f; // Instantaneous drop/rise with clouds
    }
    else if (fuel_source == "wind") {
        is_dispatchable = false;
        min_stable_load_mw = 0.0f;
        ramp_rate_mw_min = 9999.0f;
    }
    else if (fuel_source == "hydro") {
        is_dispatchable = true;
        is_peaker = true; // Excellent for balancing
        min_stable_load_mw = 0.0f;
        ramp_rate_mw_min = max_output_mw * 0.50f; // Extremely fast
    }

    // 3. Capacity Estimation (The hard part)
    if (tags.count("plant:output:electricity")) {
        try {
            std::string s = tags["plant:output:electricity"];
            // Often in MW, but sometimes formatted "500 MW"
            if (s.find("MW") != std::string::npos) {
                max_output_mw = std::stof(s.substr(0, s.find("MW")));
            } else {
                max_output_mw = std::stof(s);
            }
        } catch (...) {}
    } 
    else {
        // Fallback: Estimate from Area (Solar) or Type
        if (fuel_source == "solar" && tags.count("area")) {
            // Solar Farm Density: Approx 60 MW per km^2
            // 1 km^2 = 1,000,000 m^2
            try {
                float area_sqm = std::stof(tags["area"]);
                max_output_mw = (area_sqm / 1000000.0f) * 60.0f; 
            } catch (...) { max_output_mw = 5.0f; }
        }
        else if (fuel_source == "wind") {
             // Heuristic: 2.5 MW per turbine if mapped as a node
             max_output_mw = 2.5f; 
        }
        else if (fuel_source == "nuclear") { max_output_mw = 1000.0f; } // Big default
        else if (fuel_source == "coal") { max_output_mw = 500.0f; }
    }
}

void PowerPlantHub::update_status() {
    // 1. Weather Impact (Renewables)
    // In a full sim, "capacity_factor" comes from a WeatherManager
    if (!is_dispatchable) {
        // Simulation must set capacity_factor externally (0.0 night, 1.0 noon)
        // For now, we assume simple availability.
        target_output_mw = max_output_mw * capacity_factor;
        current_output_mw = target_output_mw; // Instant response
        return;
    }

    // 2. Dispatch Logic (Thermal Plants)
    // Move current output towards target, limited by Ramp Rate.
    
    if (current_output_mw < target_output_mw) {
        // Ramping UP
        current_output_mw += ramp_rate_mw_min; // Assuming 1 min tick
        if (current_output_mw > target_output_mw) current_output_mw = target_output_mw;
        
    } else if (current_output_mw > target_output_mw) {
        // Ramping DOWN
        current_output_mw -= ramp_rate_mw_min;
        if (current_output_mw < target_output_mw) current_output_mw = target_output_mw;
    }

    // 3. Min Stable Load Constraint
    // If we are commanded below min stable, we must either shut down (0) or stay at min.
    if (target_output_mw > 0.1f && current_output_mw < min_stable_load_mw) {
        current_output_mw = min_stable_load_mw;
    }
}

void PowerPlantHub::set_target_output(float target_mw) {
    if (!is_dispatchable) return; // Cannot command the sun

    // Clamp to nameplate capacity
    if (target_mw > max_output_mw) target_output_mw = max_output_mw;
    else if (target_mw < 0.0f) target_output_mw = 0.0f;
    else target_output_mw = target_mw;
}