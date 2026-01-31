#ifndef PIPELINE_STATION_HPP
#define PIPELINE_STATION_HPP

#include "BaseSupplyLine.hpp"

struct PipelineStationState {
    float mechanical_health;     
    float cooling_system_health;
    float power_availability;    
    float target_load;           
    float output_efficiency;     

    float unc_mech, unc_cool, unc_pow, unc_load;
    
    long long last_update;
};

class PipelineStation : public BaseSupplyLine {
public:
    PipelineStation(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    PipelineStationState current_state;
    float process_noise;
    void apply_signal(const json& sig);
};

#endif