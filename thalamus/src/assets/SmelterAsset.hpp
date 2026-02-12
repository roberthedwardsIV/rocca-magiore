#ifndef SMELTER_ASSET_HPP
#define SMELTER_ASSET_HPP

#include "BaseAsset.hpp"

struct SmelterState {
    // --- PHYSICAL STATE (The Sensors) ---
    float furnace_temperature_k;   // VIIRS Signal (Target ~1400K)
    float so2_emissions_tpd;       // Sentinel-5P (Activity proxy)
    float acid_storage_fill_pct;   // Logistic constraint (0.0 - 1.0)
    float power_grid_load;         // 0.0 - 1.0 (Grid stability)
    float op_health;               // Calculated from above

    // --- OPERATIONAL METRICS ---
    float nameplate_capacity_tpd;  // Max throughput
    float current_throughput_tpd;  // Actual flow
    float recovery_rate;           // % Metal recovered (e.g., 0.98)
    
    // --- FINANCIAL STATE (The Valuation) ---
    // Market Inputs
    float treatment_charges;       // TC ($/tonne concentrate)
    float refining_charges;        // RC (cents/lb)
    float acid_price;              // $/tonne (Byproduct credit)
    float energy_cost_mwh;         // $/MWh
    
    // Financials
    float revenue_annual;
    float opex_annual;
    float ebitda;
    float base_multiple;           // EV/EBITDA target
    float enterprise_value;        // THE SIGNAL TARGET
    
    // Risk/Valuation Factors
    float wacc;
    float threat_level;            // From external events (quakes)

    // Uncertainties (Kalman)
    float unc_temp, unc_so2, unc_fin; 

    long long last_update;
};

class SmelterAsset : public BaseAsset {
public:
    SmelterAsset(int id, std::string name);
    
    // Standard Interface
    void process_packet(const json& sig) override;
    json get_json_state() const override;
    void update(long long current_time) override;

protected:
    SmelterState current_state;
    float process_noise;
    
    // logic
    void apply_signal(const json& sig);
    void recalculate_valuation(); // <--- The missing link
};

#endif