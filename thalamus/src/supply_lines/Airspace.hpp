#ifndef AIRSPACE_HPP
#define AIRSPACE_HPP

#include "BaseSupplyLine.hpp"

struct AirspaceState {
    float navigability;    
    float traffic_volume;  
    
    float risk_index;      
    
    float congestion_penalty; 

    float unc_nav, unc_vol, unc_risk;
    long long last_update;
};

class Airspace : public BaseSupplyLine {
public:
    Airspace(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    AirspaceState current_state;
    float process_noise;
    void apply_signal(const json& sig);
};

#endif