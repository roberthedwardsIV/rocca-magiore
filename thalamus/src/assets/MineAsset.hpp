#ifndef MINE_ASSET_HPP
#define MINE_ASSET_HPP

#include "BaseAsset.hpp"

struct MineState {
    float production_rate;  // tonnes per quarter 
    float proven_reserves;  // total tonnes
    
    float risk_free_rate;    // DGS10 
    float corporate_spread;  // BAMLC0A0CM

    float tax_rate;          // T 
    float debt_weight;       // D/V
    float equity_weight;     // E/V
    
    float beta;              // Relative volatility
    float commodity_price;   // Spot prices 

    float op_health;         
    float threat_level;      
    
    float cost_per_unit;    // $ per tonne mined 

    float cost_of_debt;      // Rd
    float cost_of_equity;    // Re
    float wacc;              // Final Discount Rate
    float npv;               // Net Present Value

    float unc_prod, unc_res, unc_cost, unc_op, unc_threat; 

    long long last_update;
};

class MineAsset : public BaseAsset {
public:
    MineAsset(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;

protected:
    MineState current_state;
    float process_noise;
    
    void apply_signal(const json& sig);
    void recalculate_valuation(); 
};

#endif