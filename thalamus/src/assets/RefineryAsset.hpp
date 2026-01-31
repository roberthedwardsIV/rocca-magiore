#ifndef REFINERY_ASSET_HPP
#define REFINERY_ASSET_HPP

#include "BaseAsset.hpp"

struct RefineryState {
    float throughput_rate;   
    float storage_level;     
    float refining_cost;     
    float op_health;         
    float containment_risk;  
    
    float unc_thru, unc_store, unc_cost, unc_op, unc_risk;
    
    long long last_update;
};

class RefineryAsset : public BaseAsset {
public:
    RefineryAsset(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    RefineryState current_state;
    float process_noise;
    void apply_signal(const json& sig);
};

#endif