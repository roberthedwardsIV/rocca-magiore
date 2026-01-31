#ifndef PIPELINE_LINE_HPP
#define PIPELINE_LINE_HPP

#include "BaseSupplyLine.hpp"

struct PipelineLineState {
    float structural_integrity; 
    float blockage_severity;    
    float target_flow_rate;     
    float current_pressure_psi; 
    float effective_flow_rate;  

    float unc_struct, unc_block, unc_flow;
    
    long long last_update;
};

class PipelineLine : public BaseSupplyLine {
public:
    PipelineLine(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    PipelineLineState current_state;
    float process_noise;
    void apply_signal(const json& sig);
};

#endif