#ifndef PIPELINE_ROUTE_HPP
#define PIPELINE_ROUTE_HPP

#include "BaseRoute.hpp"

class PipelineRoute : public BaseRoute {
public:
    // --- PHYSICAL GEOMETRY ---
    float diameter_inches;     // The primary capacity constraint (e.g., 48" vs 12")
    float max_pressure_psi;    // Maximum Allowable Operating Pressure (MAOP)
    float wall_thickness_mm;   // Determines burst pressure
    
    // --- PRODUCT & PHYSICS ---
    std::string product_type;  // "oil", "gas", "water", "hydrogen", "refined_products"
    bool is_reversible;        // Can flow direction be switched?
    
    // --- DYNAMIC STATE ---
    float current_flow_rate;   // Barrels per day (Oil) or MMscf/d (Gas)
    float current_pressure_psi;// Real-time pressure reading
    bool leak_detected;        // Integrity failure
    bool maintenance_mode;     // Pigging / Inspection

    // --- OPERATIONAL ---
    float max_capacity_bpd;    // Calculated max flow (Barrels Per Day) - Oil
    float max_capacity_mmscfd; // Calculated max flow (Million Std Cubic Feet/Day) - Gas

    // Constructor
    PipelineRoute(long long id, std::string name);

    // Parses "substance", "pressure", "diameter", "man_made=pipeline"
    void parse_osm_tags();

    // Calculates max capacity based on Diameter + Pressure Limit
    void update_metrics();
};

#endif