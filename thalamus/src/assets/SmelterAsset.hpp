#ifndef SMELTER_ASSET_HPP
#define SMELTER_ASSET_HPP

#include "BaseAsset.hpp"

struct SmelterState {
    float furnace_temperature_k;   
    float so2_emissions_tpd;       
    float acid_storage_fill_pct;   
    float power_grid_load;         
    float op_health;               

    float nameplate_capacity_tpd;  
    float current_throughput_tpd;  
    float recovery_rate;           
    
    float treatment_charges;       
    float refining_charges;        
    float acid_price;              
    float energy_cost_mwh;         
    
    float revenue_annual;
    float opex_annual;
    float ebitda;
    float base_multiple;           
    float enterprise_value;        
    
    float wacc;
    float threat_level;            

    float unc_temp, unc_so2, unc_fin;
    long long last_update;
};

class SmelterAsset : public BaseAsset {
public:
    SmelterAsset(int id, std::string name);
    void process_packet(const json& sig) override;
    json get_json_state() const override;
    
    void update(long long current_time); // Removed 'override'

protected:
    SmelterState current_state;
    float process_noise;
    
    void apply_signal(const json& sig);
    void recalculate_valuation();
};

#endif