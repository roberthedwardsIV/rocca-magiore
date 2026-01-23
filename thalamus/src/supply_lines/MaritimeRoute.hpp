#ifndef MARITIME_ROUTE_HPP
#define MARITIME_ROUTE_HPP

#include "BaseSupplyLine.hpp"

struct MaritimeRouteState {
    float navigability;
    float risk_index;
    
    float unc_nav, unc_risk; 
    long long last_update;
};

class MaritimeRoute : public BaseSupplyLine {
public:
    MaritimeRoute(int id, std::string name);

    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    MaritimeRouteState current_state;
    float process_noise;

    void apply_signal(const json& sig);
};

#endif