#ifndef MARITIME_PORT_HPP
#define MARITIME_PORT_HPP

#include "BaseSupplyLine.hpp"

struct MaritimePortState {
    float berth_availability; 
    float crane_health;      
    float yard_utilization;   
    float effective_throughput; 
    float processing_delay_days; 

    float unc_berth, unc_crane, unc_yard;
    
    long long last_update;
};

class MaritimePort : public BaseSupplyLine {
public:
    MaritimePort(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    MaritimePortState current_state;
    float process_noise;
    void apply_signal(const json& sig);
};

#endif