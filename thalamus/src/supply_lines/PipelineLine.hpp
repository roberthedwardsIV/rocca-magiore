#ifndef PIPELINE_LINE_HPP
#define PIPELINE_LINE_HPP

#include "BaseSupplyLine.hpp"

struct PipelineLineState {
    float containment_integrity; 
    float flow_rate;             
    
    float unc_int, unc_flow; 
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