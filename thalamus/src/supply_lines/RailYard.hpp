#ifndef RAIL_YARD_HPP
#define RAIL_YARD_HPP

#include "BaseSupplyLine.hpp"

struct RailYardSpecs {
    float base_dwell_time_hours;     
    float jam_threshold;             
    float gridlock_penalty_factor;   
    float critical_failure_threshold;
};

struct RailYardState {
    float switch_health;         
    float labor_availability;    
    float yard_occupancy;        
    float current_dwell_time_hours; 

    float unc_sw, unc_lab, unc_occ;
    
    long long last_update;
};

class RailYard : public BaseSupplyLine {
public:
    RailYard(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    RailYardState current_state;
    RailYardSpecs specs;
    float process_noise;
    
    void apply_signal(const json& sig);
};

#endif