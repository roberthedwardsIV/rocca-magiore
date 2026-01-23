#ifndef RAIL_LINE_HPP
#define RAIL_LINE_HPP

#include "BaseSupplyLine.hpp"

struct RailLineState {
    float track_integrity; 
    float flow_capacity;  
    
    float unc_int, unc_flow; 
    long long last_update;
};

class RailLine : public BaseSupplyLine {
public:
    RailLine(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    RailLineState current_state;
    float process_noise;

    void apply_signal(const json& sig);
};

#endif