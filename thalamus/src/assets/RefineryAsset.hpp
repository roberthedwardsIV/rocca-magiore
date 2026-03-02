// VVV FILE: ./thalamus/src/assets/RefineryAsset.hpp VVV
#ifndef REFINERY_ASSET_HPP
#define REFINERY_ASSET_HPP

#include "BaseAsset.hpp"

struct RefineryState {
    float nameplate_capacity; // Max Tonnes per day (tpd)
    float throughput_rate;    // Current tpd
    float ore_inventory;      // 0.0 to 1.0 
    float op_health;          // 0.0 to 1.0
    float containment_risk;   // 0.0 to 1.0 

    float metal_spot_price;   // $/tonne
    float ore_cost_basis;     // $/tonne
    float processing_cost;    // $/tonne (SEC COGS)
    float fixed_costs;        // Annual Fixed/Capex ($) (SEC Capex)
    float base_multiple;      // EV/EBITDA

    float recovery_rate;      // % extracted (0.0 - 1.0)
    
    float utilization_rate;   
    float effective_cost;     
    float smelting_margin;    // (Spot * Recovery) - OreCost
    float gross_profit;       // Annualized
    float ebitda;             
    float enterprise_value;   
    float adjusted_multiple;  

    float unc_thru, unc_inv, unc_op, unc_risk;
    
    long long last_update;
};

class RefineryAsset : public BaseAsset {
public:
    RefineryAsset(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    RefineryState current_state;
    float process_noise;
    
    void apply_signal(const json& sig);
    void recalculate_valuation(); 
};

#endif
// ^^^ END FILE: ./thalamus/src/assets/RefineryAsset.hpp ^^^