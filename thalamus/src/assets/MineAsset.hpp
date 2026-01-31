#ifndef MINE_ASSET_HPP
#define MINE_ASSET_HPP

#include "BaseAsset.hpp"

struct MineState {
    float production_rate;   
    float proven_reserves;   
    float cost_per_unit;     
    float op_health;         
    float threat_level;      
    
    float unc_prod, unc_res, unc_cost, unc_op, unc_threat; 

    long long last_update;
};

class MineAsset : public BaseAsset {
public:
    MineAsset(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    MineState current_state;
    float process_noise;
    void apply_signal(const json& sig);
};

#endif