// VVV FILE: ./thalamus/src/assets/MineAsset.cpp VVV
#include "MineAsset.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

// Constructor
MineAsset::MineAsset(int id, std::string name) 
    : BaseAsset(id, name, "mine") {
    
    this->process_noise = 0.0005f;
    current_state = {
        100000.0f,      // Production rate
        2000000.0f,     // Proven reserves
        
        0.04f,          // Risk Free Rate         
        0.015f,         // Corporate spread

        0.25f,          // Tax rate 
        0.40f,          // Debt weight
        0.60f,          // Equity weight

        1.2f,           // Beta
        9000.0f,        // Commodity Price

        1.0f,           // Operational health
        0.0f,           // Threat level

        8500.0f,        // $ per tonne mined (COGS)
        10000000.0f,    // NEW: Fixed costs / Capex ($10M safe default)

        0.0f,           // Cost of debt
        0.0f,           // Cost equity
        0.0f,           // WACC Base Discount
        0.0f,           // Net present value

        0.5f, 0.5f, 0.5f, 0.5f, 0.5f,  // Uncertainties

        0LL             // Last update time
    };
    recalculate_valuation();
}


// Packet processor
void MineAsset::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(asset_mutex);
    apply_signal(sig);
    recalculate_valuation(); 
}


// Signal applier
void MineAsset::apply_signal(const json& sig) {
    // Standard Process Noise
    current_state.unc_prod += process_noise;
    current_state.unc_cost += process_noise;
    current_state.unc_op += process_noise;
    current_state.unc_threat += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    // --- 1. CORPORATE FILINGS (Direct Updates) ---
    // Ground Truth from 10-Q/10-K Reports. Overwrites estimated state.
    if (category == "filing") {
        if (sig.contains("cost_per_unit")) {
            current_state.cost_per_unit = sig["cost_per_unit"].get<float>();
            current_state.unc_cost = process_noise; // hard set for direct cost filings
        }
        // NEW: Ingest Capex/Overhead from SEC Auditor
        if (sig.contains("fixed_costs")) {
            current_state.fixed_costs = sig["fixed_costs"].get<float>();
        }
        if (sig.contains("tax_rate")) {
            current_state.tax_rate = sig["tax_rate"].get<float>();
        }
        if (sig.contains("debt_weight")) {
            current_state.debt_weight = sig["debt_weight"].get<float>();
        }
        if (sig.contains("equity_weight")) {
            current_state.equity_weight = sig["equity_weight"].get<float>();
        }
        if (sig.contains("proven_reserves")) {
            current_state.proven_reserves = sig["proven_reserves"].get<float>();
            current_state.unc_res = process_noise;
        }
        // hard set for direct production_rate filings
        if (sig.contains("production_rate")) {
            current_state.production_rate = sig["production_rate"].get<float>();
            current_state.unc_prod = process_noise;
        }
    }

    // Macro updates (from FRED)
    else if (category == "rates") {
        if (sig["data"].contains("risk_free_rate")) {
            current_state.risk_free_rate = sig["data"]["risk_free_rate"];
        }
        if (sig["data"].contains("corporate_spread")) {
            current_state.corporate_spread = sig["data"]["corporate_spread"];
        }
    }

    // Market updates (from market data broker)
    else if (category == "market") {
        if (sig.contains("price")) {
            current_state.commodity_price = sig["price"].get<float>();
        }
        if (sig.contains("beta")) {
            current_state.beta = sig["beta"].get<float>();
        }
    }

    // Threat updates (via frontal_lobe/sensory_receptors)
    else if (category == "threat") {
        float K = current_state.unc_threat / (current_state.unc_threat + R);
        current_state.threat_level += K * (severity - current_state.threat_level);
        current_state.unc_threat *= (1.0f - K);
    }

    // Production updates (via frontal_lobe/sensory_receptors)
    else if (category == "production") {
        if (sig.contains("value")) {
            float z_prod = sig["value"].get<float>();
            float K = current_state.unc_prod / (current_state.unc_prod + R);
            current_state.production_rate += K * (z_prod - current_state.production_rate);
            current_state.unc_prod *= (1.0f - K);
        }
    }

    // Operational health updates (via frontal_lobe/sensory_receptors)
    else if (category == "op") {
        float z = 1.0f - severity;
        float K = current_state.unc_op / (current_state.unc_op + R);
        current_state.op_health += K * (z - current_state.op_health);
        current_state.unc_op *= (1.0f - K);
        
        // Operational issues modulate the cost posted in filings
        if (current_state.op_health < 1.0f) {
             float brokenness = (1.0f - current_state.op_health);
             current_state.cost_per_unit *= (1.0f + (brokenness * brokenness));
        }
    } 
    
    current_state.last_update = sig.value("timestamp", 0LL);
}


// Net present value calculation
void MineAsset::recalculate_valuation() {
    
    // Cost of debt (Rd) = RiskFree + CorpSpread + (Threat * EmergencyPremium)
    float risk_premium = current_state.threat_level * 0.10f; 
    current_state.cost_of_debt = current_state.risk_free_rate + current_state.corporate_spread + risk_premium;

    // Cost of Equity (CAPM) = RiskFree + Beta * (MarketRiskPremium)
    float market_risk_premium = 0.055f;
    current_state.cost_of_equity = current_state.risk_free_rate + (current_state.beta * market_risk_premium);

    // Base discount (WACC): (E/V * Re) + (D/V * Rd * (1 - T))
    float equity_component = current_state.equity_weight * current_state.cost_of_equity;
    float debt_component   = current_state.debt_weight * current_state.cost_of_debt * (1.0f - current_state.tax_rate);
    current_state.wacc = equity_component + debt_component;
    if (current_state.wacc < 0.01f) current_state.wacc = 0.01f; // prevent's 0 values that'd break calculations

    // Cash flow + net present value calculation
    float annual_production = std::max(1.0f, current_state.production_rate);
    float lom_years = current_state.proven_reserves / annual_production;
    if (lom_years > 50.0f) lom_years = 50.0f; // cap mine age at 50 years

    float margin = current_state.commodity_price - current_state.cost_per_unit;
    float effective_production = annual_production * current_state.op_health;
    
    // NEW: EBITDA now accurately subtracts SEC audited fixed costs (Capex/Overhead)
    float ebitda = (margin * effective_production) - current_state.fixed_costs;
    
    float free_cash_flow = ebitda * (1.0f - current_state.tax_rate);

    // PV of Annuity Factor: [1 - (1+r)^-n] / r
    float discount_factor = (1.0f - std::pow(1.0f + current_state.wacc, -lom_years)) / current_state.wacc;
    
    // NEW: Bounded at 0. A rationally managed asset with negative DCF holds an abandonment option value of $0.
    current_state.npv = std::max(0.0f, free_cash_flow * discount_factor);
}


// JSON packager
json MineAsset::get_json_state() const {
    std::lock_guard<std::mutex> lock(asset_mutex);
    return {
        {"asset_id", asset_id},
        {"name", name},
        {"entity_type", entity_type},
        {"npv", current_state.npv},
        {"wacc", current_state.wacc},
        {"beta", current_state.beta},
        {"cost_of_debt", current_state.cost_of_debt},
        {"cost_of_equity", current_state.cost_of_equity},
        {"production_rate", current_state.production_rate},
        {"cost_per_unit", current_state.cost_per_unit},
        {"fixed_costs", current_state.fixed_costs}, // Included for dashboard visibility
        {"last_update", current_state.last_update}
    };
}
// ^^^ END FILE: ./thalamus/src/assets/MineAsset.cpp ^^^