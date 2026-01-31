#ifndef RAIL_LINE_HPP
#define RAIL_LINE_HPP

#include "BaseSupplyLine.hpp"

struct RailSpecs {
    float design_speed_kmh;          
    float critical_damage_threshold; 
    float signal_fail_speed_factor;  
    float congestion_soft_cap;       
    float congestion_scaling_factor; 
};

struct RailLineState {
    float track_integrity;       
    float electrification_status;
    float signal_health;         
    float congestion_level;     
    float max_safe_speed_kmh;   

    float unc_track, unc_elec, unc_sig, unc_cong;

    long long last_update;
};

class RailLine : public BaseSupplyLine {
public:
    RailLine(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    RailLineState current_state;
    RailSpecs specs; 
    float process_noise;
    
    void apply_signal(const json& sig);
};

#endif