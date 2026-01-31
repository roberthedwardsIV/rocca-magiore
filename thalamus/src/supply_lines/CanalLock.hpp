#ifndef CANAL_LOCK_HPP
#define CANAL_LOCK_HPP

#include "BaseSupplyLine.hpp"

struct CanalLockState {
    float mechanical_health; 
    float water_level;       
    float transit_capacity;  
    float queue_size;        

    float unc_mech, unc_water, unc_cap;
    
    long long last_update;
};

class CanalLock : public BaseSupplyLine {
public:
    CanalLock(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    CanalLockState current_state;
    float process_noise;
    void apply_signal(const json& sig);
};

#endif