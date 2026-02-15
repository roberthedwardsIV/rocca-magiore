#ifndef PIPELINE_ROUTE_HPP
#define PIPELINE_ROUTE_HPP

#include "BaseRoute.hpp"

class PipelineRoute : public BaseRoute {
public:
    // Static
    float diameter_inches;     // Pipe Diameter (in.)
    float max_pressure_psi;    // Maximum Allowable Operating Pressure (PSI)
    float wall_thickness_mm;   // Pipe Thickness (mm)
    bool is_reversible;        // Bi-directional capability
    std::string product_type;  // "tailings", "concentrate", "water"
    float product_density;     // Product Density (kg/m3) 
    float product_viscosity;   // cSt 

    // Dynamic
    float current_flow_rate;   // bpd or MMscfd 
    float current_pressure_psi;// Real-time pressure (PSI)
    bool leak_detected;        // Integrity failure
    bool maintenance_mode;     // Pigging / Inspection

    // Calculated
    float max_capacity_bpd;    // Liquid Capacity
    float max_capacity_mmscfd; // Gas Capacity
    float efficiency_factor;   // 1.0 (Clean) -> 0.7 (Waxy buildup/Friction)

    PipelineRoute(long long id, std::string name);

    void parse_osm_tags();     
    void update_metrics();     
    
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};

#endif