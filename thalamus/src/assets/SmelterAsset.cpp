#include "SmelterAsset.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

SmelterAsset::SmelterAsset(int id, std::string name) 
    : BaseAsset(id, name, "smelter") {
    
    this->process_noise = 0.003f;
    
    // Initialize with "Steady State" defaults
    current_state = {
        // Physical
        1450.0f,    // Temp K (Healthy Flash Furnace)
        500.0f,     // SO2 tpd (Active)
        0.2f,       // Acid Tank (Empty)
        1.0f,       // Grid (Stable)
        1.0f,       // Op Health

        // Operational
        2500.0f,    // Nameplate (Tonnes/day)
        2400.0f,    // Current Throughput
        0.975f,     // Recovery

        // Financial Inputs
        85.0f,      // TC ($/t)
        0.085f,     // RC ($/lb)
        120.0f,     // Acid Credit ($/t)
        60.0f,      // Energy ($/MWh)

        // Outputs
        0.0f, 0.0f, 0.0f, // Rev, Opex, EBITDA
        6.5f,             // Multiple (Smelters trade lower than mines)
        0.0f,             // EV
        0.08f,            // WACC (8%)
        0.0f,             // Threat

        0.5f, 0.5f, 0.5f, // Uncertainties
        0LL
    };
    
    recalculate_valuation();
}

void SmelterAsset::update(long long current_time) {
    // Decay logic: If we lose satellite signal, confidence drops and temp "cools"
    // This forces the system to seek fresh data.
    current_state.unc_temp += 0.001f;
}

void SmelterAsset::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(asset_mutex);
    apply_signal(sig);
    recalculate_valuation();
}

void SmelterAsset::apply_signal(const json& sig) {
    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");

    // 1. THERMAL SIGNAL (VIIRS)
    if (category == "thermal") {
        float z_temp = sig.value("temp_k", 0.0f);
        float K = current_state.unc_temp / (current_state.unc_temp + R);
        current_state.furnace_temperature_k += K * (z_temp - current_state.furnace_temperature_k);
        current_state.unc_temp *= (1.0f - K);
    }
    
    // 2. GAS SIGNAL (Sentinel-5P)
    else if (category == "emissions") {
        float z_so2 = sig.value("so2_val", 0.0f);
        float K = current_state.unc_so2 / (current_state.unc_so2 + R);
        current_state.so2_emissions_tpd += K * (z_so2 - current_state.so2_emissions_tpd);
        current_state.unc_so2 *= (1.0f - K);
    }

    // 3. FINANCIAL FILINGS (Ground Truth)
    else if (category == "filing") {
        if (sig.contains("tcrc")) current_state.treatment_charges = sig["tcrc"];
        if (sig.contains("capacity")) current_state.nameplate_capacity_tpd = sig["capacity"];
        if (sig.contains("recovery")) current_state.recovery_rate = sig["recovery"];
    }

    // 4. DERIVE OPERATIONAL HEALTH
    // Smelting physics: Temp must be > 1300K for Flash Smelting. 
    // If Temp < 1000K, the bath is "freezing" -> Critical Failure.
    float temp_health = 1.0f;
    if (current_state.furnace_temperature_k < 1300.0f) {
        temp_health = std::max(0.0f, (current_state.furnace_temperature_k - 300.0f) / 1000.0f);
    }
    
    // Acid Containment: If acid tanks represent > 95% capacity, we MUST throttle.
    float acid_throttle = (current_state.acid_storage_fill_pct > 0.95f) ? 0.0f : 1.0f;

    current_state.op_health = temp_health * acid_throttle * current_state.power_grid_load;
    
    // Throughput is purely a function of Nameplate * Health
    current_state.current_throughput_tpd = current_state.nameplate_capacity_tpd * current_state.op_health;

    current_state.last_update = sig.value("timestamp", 0LL);
}

void SmelterAsset::recalculate_valuation() {
    // 1. REVENUE CALCULATION
    // Smelters make money on TC/RCs, not the copper price itself (usually).
    // Revenue = (Throughput * TC) + (Recovered Metal * RC) + (Acid * Price)
    
    float annual_tonnes = current_state.current_throughput_tpd * 365.0f;
    
    // TC Revenue
    float rev_tc = annual_tonnes * current_state.treatment_charges;
    
    // RC Revenue (Approx 2204 lbs per tonne * Grade approx 30% for concentrate)
    float lbs_metal = annual_tonnes * 0.30f * 2204.62f * current_state.recovery_rate;
    float rev_rc = lbs_metal * current_state.refining_charges; // cents/lb logic handled externally or normalized
    
    // Acid Credits (Approx 3t acid per 1t copper produced)
    float acid_tonnes = (lbs_metal / 2204.62f) * 3.0f;
    float rev_acid = acid_tonnes * current_state.acid_price;

    current_state.revenue_annual = rev_tc + rev_rc + rev_acid;

    // 2. COST CALCULATION
    // Energy is the killer for smelters.
    float mwh_per_tonne = 0.5f; // Flash smelting is efficient, approx 500kWh/t
    float cost_energy = annual_tonnes * mwh_per_tonne * current_state.energy_cost_mwh;
    float cost_fixed = 50000000.0f; // $50M base fixed cost
    
    current_state.opex_annual = cost_energy + cost_fixed;

    // 3. VALUATION (EV)
    current_state.ebitda = current_state.revenue_annual - current_state.opex_annual;
    
    // Operational Risk Penalty to Multiple
    // If op_health is 50%, the multiple collapses because the asset is unreliable.
    float risk_penalty = (1.0f - current_state.op_health) * 2.0f;
    float effective_multiple = std::max(1.0f, current_state.base_multiple - risk_penalty);

    current_state.enterprise_value = current_state.ebitda * effective_multiple;
}

json SmelterAsset::get_json_state() const {
    std::lock_guard<std::mutex> lock(asset_mutex);
    return {
        {"asset_id", asset_id},
        {"name", name},
        {"entity_type", entity_type},
        {"op_health", current_state.op_health},
        {"furnace_temp_k", current_state.furnace_temperature_k},
        {"throughput_tpd", current_state.current_throughput_tpd},
        {"ebitda", current_state.ebitda},
        {"enterprise_value", current_state.enterprise_value}, // <--- The key signal for StockTicker
        {"so2_emissions", current_state.so2_emissions_tpd},
        {"last_update", current_state.last_update}
    };
}