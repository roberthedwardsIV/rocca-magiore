#include "PipelineRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- FLUID PHYSICS CONSTANTS ---
    // Reference: 12-inch pipe baseline
    constexpr float BASE_OIL_FLOW_BPD = 80000.0f;       
    constexpr float BASE_GAS_FLOW_MMSCFD = 60.0f;       
    
    // Densities (kg/m3) 
    constexpr float DENSITY_WATER = 1000.0f;
    constexpr float DENSITY_CRUDE_LIGHT = 850.0f;
    constexpr float DENSITY_COPPER_CONCENTRATE = 2100.0f; 
    constexpr float DENSITY_IRON_SLURRY = 2500.0f;        
    constexpr float DENSITY_TAILINGS = 1600.0f;           
    
    // Viscosity (Centistokes)
    constexpr float VISCOSITY_WATER = 1.0f;
    constexpr float VISCOSITY_SLURRY_BASE = 30.0f; // Non-Newtonian approximation
    
    // Scaling Exponents
    constexpr float EXPONENT_DIAMETER_LIQUID = 2.5f;    
    constexpr float EXPONENT_DIAMETER_GAS = 2.53f;      
    constexpr float EXPONENT_PRESSURE_GAS = 0.51f;      
}

// Constructor for initialization
PipelineRoute::PipelineRoute(long long id, std::string name)
    : BaseRoute(id, name, "pipeline") {
    
    // Default values
    diameter_inches = 12.0f;
    max_pressure_psi = 1000.0f; 
    wall_thickness_mm = 10.0f;
    is_reversible = false;   
    product_type = "concentrate";
    product_density = DENSITY_COPPER_CONCENTRATE;
    product_viscosity = VISCOSITY_SLURRY_BASE;
    current_flow_rate = 0.0f;
    current_pressure_psi = 800.0f; 
    leak_detected = false;
    maintenance_mode = false;
    efficiency_factor = 1.0f;   
    max_capacity_bpd = 0.0f;
    max_capacity_mmscfd = 0.0f;
    min_safe_flow_bpd = 0.0f; 
}


// Direct updates to static fields from OSM tags
void PipelineRoute::parse_osm_tags() {
    // Product Type + Properties
    if (tags.count("substance")) product_type = tags["substance"];
    else if (tags.count("type")) product_type = tags["type"];

    if (product_type == "concentrate" || product_type == "slurry") {
        product_density = DENSITY_COPPER_CONCENTRATE;
        product_viscosity = VISCOSITY_SLURRY_BASE;
    } 
    else if (product_type == "tailings") {
        product_density = DENSITY_TAILINGS;
        product_viscosity = VISCOSITY_SLURRY_BASE * 1.5f; 
    }
    else if (product_type == "iron_ore" || product_type == "magnetite") {
        product_density = DENSITY_IRON_SLURRY;
        product_viscosity = VISCOSITY_SLURRY_BASE * 2.0f;
    }
    else if (product_type == "oil" || product_type == "crude") {
        product_density = DENSITY_CRUDE_LIGHT;
        product_viscosity = 10.0f;
    } else if (product_type == "gas") {
        product_density = 0.7f;
    }

    // Pipe Diameter
    if (tags.count("diameter")) {
        std::string d_str = tags["diameter"];
        try {
            float val = std::stof(d_str);
            if (d_str.find("in") != std::string::npos) diameter_inches = val;
            else diameter_inches = val / 25.4f; // mm to inches
        } catch (...) { diameter_inches = 12.0f; }
    }

    // Pressure
    if (tags.count("pressure")) {
        try {
            float val = std::stof(tags["pressure"]);
            if (val < 200.0f) max_pressure_psi = val * 14.5038f; 
            else max_pressure_psi = val;
        } catch (...) { max_pressure_psi = 1000.0f; }
    }
    
    // High pressure default for long-distance concentrate lines
    if (product_type == "concentrate" && max_pressure_psi < 1000.0f) {
        max_pressure_psi = 2000.0f; 
    }
    
    update_metrics();
}


// Updates to dynamic + calculated fields from Thalamus signaling
void PipelineRoute::update_metrics() {
    if (leak_detected || maintenance_mode) {
        current_flow_rate = 0.0f;
        max_capacity_bpd = 0.0f;
        return; 
    }

    // Liquid + Solid Product physics
    if (product_type != "gas" && product_type != "hydrogen") {
        float scaling_factor = std::pow(diameter_inches / 12.0f, EXPONENT_DIAMETER_LIQUID);
        
        // Viscosity Adjustment
        float viscosity_factor = std::sqrt(10.0f / product_viscosity);
        
        // Density Adjustment 
        float density_factor = std::sqrt(DENSITY_CRUDE_LIGHT / product_density);

        max_capacity_bpd = BASE_OIL_FLOW_BPD * scaling_factor * viscosity_factor * density_factor * efficiency_factor;

        // --- CRITICAL DEPOSITION VELOCITY (Durand-Condolios) ---
        // Vc = F_L * sqrt(2 * g * D * (S_s - 1))
        // If flow drops below this, solids settle
        if (product_type == "concentrate" || product_type == "tailings" || product_type == "slurry") {
            float D_meters = diameter_inches * 0.0254f;
            float S_s = product_density / DENSITY_WATER; 
            float F_L = 1.1f; // Froude number for uniform sand/grit
            
            float v_crit_ms = F_L * std::sqrt(2.0f * 9.81f * D_meters * (S_s - 1.0f));
            
            // Convert velocity (m/s) to Flow Rate (BPD)
            // Area = pi * r^2
            float area = 3.14159f * std::pow(D_meters / 2.0f, 2.0f);
            float q_m3s = v_crit_ms * area;
            min_safe_flow_bpd = q_m3s * 543439.0f; // m3/s to bpd
        } else {
            min_safe_flow_bpd = 0.0f; 
        }
        
        // Pressure derating
        float pressure_ratio = current_pressure_psi / max_pressure_psi;
        if (pressure_ratio < 0.6f) {
            // Pumps struggling -> Capacity drops
            max_capacity_bpd *= pressure_ratio * 1.5f; 
        }
    }

    // Gas Product Physics
    else {
        float scaling_factor = std::pow(diameter_inches / 12.0f, EXPONENT_DIAMETER_GAS);
        float pressure_factor = std::pow(current_pressure_psi / 1000.0f, EXPONENT_PRESSURE_GAS);
        max_capacity_mmscfd = BASE_GAS_FLOW_MMSCFD * scaling_factor * pressure_factor * efficiency_factor;
        min_safe_flow_bpd = 0.0f;
    }
}


// Main signal routing function
void PipelineRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::string category = sig.value("category", "none");

    // Telemetry
    if (category == "telemetry" || category == "flow") {
        if (sig.contains("pressure")) current_pressure_psi = sig["pressure"];
        if (sig.contains("flow_rate")) current_flow_rate = sig["flow_rate"];
    }

    // Integrity 
    else if (category == "integrity" || category == "leak") {
        if (sig.value("leak_detected", false)) {
            leak_detected = true;
            current_flow_rate = 0.0f; 
        } else {
            leak_detected = false; 
        }
    }

    // Maintenance 
    else if (category == "maintenance") {
        maintenance_mode = sig.value("active", false);
        // Slurry lines wear out; pigging/rotation restores efficiency
        if (!maintenance_mode) efficiency_factor = 1.0f; 
    }
    
    // Slurry density change 
    else if (category == "batch") {
        if (sig.contains("density")) product_density = sig["density"];
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}


// JSON packager for archival
json PipelineRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(route_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"product", product_type},
        {"flow_rate", current_flow_rate},
        {"min_safe_flow", min_safe_flow_bpd},
        {"pressure_psi", current_pressure_psi},
        {"capacity_bpd", max_capacity_bpd},
        {"capacity_mmscfd", max_capacity_mmscfd},
        {"density", product_density},
        {"leak", leak_detected},
        {"last_update", last_update}
    };
}