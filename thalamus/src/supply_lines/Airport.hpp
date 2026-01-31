#ifndef AIRPORT_HPP
#define AIRPORT_HPP

#include "BaseSupplyLine.hpp"

struct AirportState {
    float runway_integrity;   
    float cargo_throughput;   
    float atc_efficiency;     
    float fuel_availability;  
    float processing_delay_hours; 

    float unc_run, unc_cargo, unc_atc, unc_fuel;
     
    long long last_update;
};

class Airport : public BaseSupplyLine {
public:
    Airport(int id, std::string name);

    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    AirportState current_state;
    float process_noise;

    void apply_signal(const json& sig);
};

#endif