#ifndef CANAL_ROUTE_HPP
#define CANAL_ROUTE_HPP

#include "BaseSupplyLine.hpp"

struct CanalRouteState {
    float navigability;   
    float depth_health;  
    
    float unc_nav, unc_depth; 
    long long last_update;
};

class CanalRoute : public BaseSupplyLine {
public:
    CanalRoute(int id, std::string name);

    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    CanalRouteState current_state;
    float process_noise;

    void apply_signal(const json& sig);
};

#endif