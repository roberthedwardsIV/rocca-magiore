#ifndef RAIL_YARD_HPP
#define RAIL_YARD_HPP

#include "BaseSupplyLine.hpp" 

struct RailYardState {
    float processing_health; 
    float utilization;      
    
    float unc_proc, unc_util; 
    long long last_update;
};

class RailYard : public BaseSupplyLine {
public:
    RailYard(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    RailYardState current_state;
    float process_noise;

    void apply_signal(const json& sig);
};

#endif