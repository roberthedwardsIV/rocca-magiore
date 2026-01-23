#ifndef AIRSPACE_HPP
#define AIRSPACE_HPP

#include "BaseSupplyLine.hpp"

struct AirspaceState {
    float navigability;   // 1.0 = open, 0.0 = closed/restricted
    float risk_index;     // 0.0 = safe, 1.0 = high (ash, storm, war)
    
    float unc_nav, unc_risk; 
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