#include "PipelineRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

PipelineRoute::PipelineRoute(long long id, std::string name)
    : BaseRoute(id, name, "pipeline") {
    
    // Default Physical (Small gathering line)
    diameter_inches = 12.0f;  
    max_pressure_psi = 1000.0f; // Standard high pressure
    wall_thickness_mm = 10.0f;
    
    // Default Product
    product_type = "oil";     // Most common for strategic supply lines
    is_reversible = false;    // Rare feature

    // Default Dynamic State
    current_flow_rate = 0.0f;
    current_pressure_psi = 800.0f; // Operating pressure
    leak_detected = false;
    maintenance_mode = false;

    // Default Capacity (Calculated later)
    max_capacity_bpd = 0.0f;
    max_capacity_mmscfd = 0.0f;
}

void PipelineRoute::parse_osm_tags() {
    // 1. Substance / Product
    if (tags.count("substance")) {
        product_type = tags["substance"];
    } else if (tags.count("type")) {
        // e.g., type=gas
        if (tags["type"] == "gas" || tags["type"] == "oil" || tags["type"] == "water") {
            product_type = tags["type"];
        }
    }

    // 2. Diameter (The #1 Capacity Factor)
    // OSM standard is millimeters, but industry often uses inches.
    if (tags.count("diameter")) {
        std::string d_str = tags["diameter"];
        try {
            float val = std::stof(d_str);
            if (d_str.find("in") != std::string::npos || d_str.find("\"") != std::string::npos) {
                diameter_inches = val; // Already inches
            } else {
                // Assume Millimeters (OSM default)
                diameter_inches = val / 25.4f; 
            }
        } catch (...) { diameter_inches = 12.0f; }
    }

    // 3. Pressure (MAOP)
    // OSM often uses "pressure" in bar
    if (tags.count("pressure")) {
        try {
            std::string p_str = tags["pressure"];
            float val = std::stof(p_str);
            if (p_str.find("bar") != std::string::npos) {
                max_pressure_psi = val * 14.5038f;
            } else if (p_str.find("psi") != std::string::npos) {
                max_pressure_psi = val;
            } else {
                // Heuristic: If value is small (<100), assume bar. If large (>100), assume psi.
                if (val < 150.0f) max_pressure_psi = val * 14.5038f; // Bar
                else max_pressure_psi = val; // PSI
            }
        } catch (...) { max_pressure_psi = 1000.0f; }
    }
    
    // 4. Usage (Interconnector vs Distribution)
    if (tags.count("usage")) {
        if (tags["usage"] == "transmission") {
            // High pressure defaults if not set
            if (max_pressure_psi < 500.0f) max_pressure_psi = 1440.0f; // Standard transmission pressure
        }
    }
}

void PipelineRoute::update_metrics() {
    // 1. Critical Failure Check
    if (leak_detected || maintenance_mode) {
        current_flow_rate = 0.0f;
        max_capacity_bpd = 0.0f;
        max_capacity_mmscfd = 0.0f;
        return; 
    }

    // 2. Capacity Estimation (Physics Heuristics)
    
    // --- OIL PIPELINES (Incompressible Flow) ---
    // Rule of Thumb: Flow is roughly proportional to Diameter^2.5
    // 12" ~ 80,000 bpd
    // 24" ~ 400,000 bpd
    // 36" ~ 1,000,000 bpd
    // 48" ~ 2,500,000 bpd
    if (product_type == "oil" || product_type == "fuel" || product_type == "crude_oil") {
        float base_bpd = 80000.0f; // Reference for 12"
        float scaling_factor = std::pow(diameter_inches / 12.0f, 2.5f);
        
        max_capacity_bpd = base_bpd * scaling_factor;
        
        // Pressure derating
        // If operating pressure drops below 50% of MAOP, flow suffers.
        float pressure_ratio = current_pressure_psi / max_pressure_psi;
        if (pressure_ratio < 0.5f) {
            // Pump efficiency loss
            max_capacity_bpd *= (pressure_ratio * 2.0f); 
        }
    }

    // --- GAS PIPELINES (Compressible Flow) ---
    // Rule of Thumb: Velocity is higher, but density is lower.
    // Measured in MMscfd (Million Standard Cubic Feet per Day)
    // 12" ~ 60 MMscfd
    // 36" ~ 1,000 MMscfd
    // 48" ~ 2,500 MMscfd
    else if (product_type == "gas" || product_type == "natural_gas") {
        float base_mmscfd = 60.0f; // Reference for 12"
        float scaling_factor = std::pow(diameter_inches / 12.0f, 2.6f); // Slightly better scaling for gas
        
        max_capacity_mmscfd = base_mmscfd * scaling_factor;

        // Gas flow is highly sensitive to pressure differential
        float pressure_ratio = current_pressure_psi / max_pressure_psi;
        max_capacity_mmscfd *= std::sqrt(pressure_ratio); // Approx Panhandle equation relationship
    }
    
    // --- WATER PIPELINES ---
    else if (product_type == "water") {
        // Similar to oil but usually lower pressure/velocity
        float base_m3_day = 10000.0f; // Reference for 12"
        float scaling_factor = std::pow(diameter_inches / 12.0f, 2.0f); // Square law (Area)
        
        // Store as generic unit (reusing BPD field for simplicity or adding new one)
        max_capacity_bpd = base_m3_day * scaling_factor; // Note: Units are m3/day here!
    }
}