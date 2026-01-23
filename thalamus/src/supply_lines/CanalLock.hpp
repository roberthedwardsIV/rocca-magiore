#ifndef CANAL_LOCK_HPP
#define CANAL_LOCK_HPP

#include "BaseSupplyLine.hpp"

struct CanalLockState {
    float mechanical_health; // 1.0 = nominal, 0.0 = gate/pump failure
    float transit_capacity;  // 1.0 = nominal flow, 0.0 = ground stop
    
    float unc_mech, unc_cap; 
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