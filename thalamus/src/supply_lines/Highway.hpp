#ifndef HIGHWAY_HPP
#define HIGHWAY_HPP

#include "BaseSupplyLine.hpp"

struct HighwayState {
    float navigability;   
    float traffic_flow;   
    
    float unc_nav, unc_flow; 
    long long last_update;
};

class Highway : public BaseSupplyLine {
public:
    Highway(int id, std::string name);

    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    HighwayState current_state;
    float process_noise;

    void apply_signal(const json& sig);
};

#endif