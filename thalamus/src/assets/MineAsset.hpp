#ifndef MINE_ASSET_HPP
#define MINE_ASSET_HPP

#include "BaseAsset.hpp"
#include <vector>

struct MineState {
    float op_health;    
    float fin_health;   
    float threat_level; 
    
    float unc_op, unc_fin, unc_threat; 
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