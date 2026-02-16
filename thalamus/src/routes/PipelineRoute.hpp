#ifndef PIPELINE_ROUTE_HPP
#define PIPELINE_ROUTE_HPP

#include "BaseRoute.hpp"

class PipelineRoute : public BaseRoute {
public:
    // Static
    float diameter_inches;     
    float max_pressure_psi;    
    float wall_thickness_mm;   
    bool is_reversible;        
    std::string product_type;  
    float product_density;     
    float product_viscosity;   

    // Dynamic
    float current_flow_rate;   
    float current_pressure_psi;
    bool leak_detected;        
    bool maintenance_mode;     

    // Calculated
    float max_capacity_bpd;    
    float max_capacity_mmscfd; 
    float efficiency_factor;   
    float min_safe_flow_bpd;   // <--- Added this

    PipelineRoute(long long id, std::string name);

    void parse_osm_tags();     
    void update_metrics();
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};
#endif