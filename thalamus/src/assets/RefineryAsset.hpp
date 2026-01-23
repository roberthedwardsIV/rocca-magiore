#ifndef REFINERY_ASSET_HPP
#define REFINERY_ASSET_HPP

#include "BaseAsset.hpp"

struct RefineryState {
    float op_health;          
    float containment_risk;   
    float fin_health;         
    
    float unc_op, unc_risk, unc_fin; 
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