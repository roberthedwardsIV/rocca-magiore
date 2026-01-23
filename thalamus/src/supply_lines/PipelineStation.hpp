#ifndef PIPELINE_STATION_HPP
#define PIPELINE_STATION_HPP

#include "BaseSupplyLine.hpp"

struct PipelineStationState {
    float pump_efficiency;   
    float pressure_gradient; 
    
    float unc_pump, unc_pres; 
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