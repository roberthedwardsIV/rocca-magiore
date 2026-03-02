// VVV FILE: ./thalamus/src/assets/RefineryAsset.cpp VVV
#include "RefineryAsset.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

// Constructor
RefineryAsset::RefineryAsset(int id, std::string name) 
    : BaseAsset(id, name, "refinery") {
    
    this->process_noise = 0.002f;
    current_state = {
        // Physical Defaults 
        3000.0f,        // Nameplate Capacity (Tonnes/day)
        2800.0f,        // Initial Throughput
        0.5f,           // Ore Inventory
        1.0f,           // Op Health
        0.0f,           // Containment Risk 

        // Financial Defaults
        9000.0f,        // Metal Spot Price ($/tonne)
        6000.0f,        // Ore Cost Basis ($/tonne)
        800.0f,         // Processing Cost ($/tonne)
        40000000.0f,    // Fixed Costs ($40M/yr safe default)
        7.0f,           // Base Multiple (EV/EBITDA)

        // Metallurgy
        0.96f,          // Recovery Rate (96%)

        // Calculated Placeholders
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,

        // Uncertainties
        0.5f, 0.5f, 0.5f, 0.5f, 

        // Last update time
        0LL
    };
    recalculate_valuation();
}


// Packet processor + repricing
void RefineryAsset::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(asset_mutex);
    apply_signal(sig);
    recalculate_valuation(); 
}

// Math to update refinery state with updated data
void RefineryAsset::apply_signal(const json& sig) {
    current_state.unc_thru += process_noise;
    current_state.unc_inv += process_noise;
    current_state.unc_op += process_noise;
    current_state.unc_risk += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    // --- 1. SEC FILINGS (Ground Truth) ---
    if (category == "filing") {
        if (sig.contains("fixed_costs")) 
            current_state.fixed_costs = sig["fixed_costs"].get<float>();
        
        if (sig.contains("cost_per_unit")) 
            current_state.processing_cost = sig["cost_per_unit"].get<float>();
        
        if (sig.contains("base_multiple")) 
            current_state.base_multiple = sig["base_multiple"].get<float>();
            
        if (sig.contains("nameplate_capacity")) 
            current_state.nameplate_capacity = sig["nameplate_capacity"].get<float>();
            
        if (sig.contains("recovery_rate"))
            current_state.recovery_rate = sig["recovery_rate"].get<float>();

        if (sig.contains("throughput")) {
            current_state.throughput_rate = sig["throughput"].get<float>();
            current_state.unc_thru = process_noise; 
        }
    }
    // --- 2. SPOT MARKETS ---
    else if (category == "market") {
        if (sig.contains("price")) { 
            current_state.metal_spot_price = sig["price"].get<float>();
        }
        if (sig.contains("ore_cost")) { 
            current_state.ore_cost_basis = sig["ore_cost"].get<float>();
        }
    }
    // --- 3. SENSORY RECEPTORS ---
    else if (category == "throughput") {
        if (sig.contains("value")) {
            float z_thru = sig["value"].get<float>();
            float K = current_state.unc_thru / (current_state.unc_thru + R);
            current_state.throughput_rate += K * (z_thru - current_state.throughput_rate);
            current_state.unc_thru *= (1.0f - K);
        }
    }
    else if (category == "op") {
        float z = 1.0f - severity;
        float K = current_state.unc_op / (current_state.unc_op + R);
        current_state.op_health += K * (z - current_state.op_health);
        current_state.unc_op *= (1.0f - K);
    } 
    else if (category == "threat") {
        float K = current_state.unc_risk / (current_state.unc_risk + R);
        current_state.containment_risk += K * (severity - current_state.containment_risk);
        current_state.unc_risk *= (1.0f - K);
    }
    else if (category == "logistics") {
        if (sig.contains("value")) {
            float z_inv = sig["value"].get<float>();
            float K = current_state.unc_inv / (current_state.unc_inv + R);
            current_state.ore_inventory += K * (z_inv - current_state.ore_inventory);
            current_state.unc_inv *= (1.0f - K);
        }
    }
    
    current_state.last_update = sig.value("timestamp", 0LL);
}

// Valuation Calculation (EV/EBITDA)
void RefineryAsset::recalculate_valuation() {
    current_state.utilization_rate = current_state.throughput_rate / std::max(1.0f, current_state.nameplate_capacity);
    
    // Recovery & Cost adjustment (from op_health)
    float health_penalty = (1.0f - current_state.op_health);
    float effective_recovery = current_state.recovery_rate - (health_penalty * 0.05f); // Max 5% yield loss
    current_state.effective_cost = current_state.processing_cost * (1.0f + (health_penalty * 0.2f)); // +20% energy cost

    // Calculate spread of refining
    float revenue_per_tonne = current_state.metal_spot_price * effective_recovery;
    current_state.smelting_margin = revenue_per_tonne - current_state.ore_cost_basis;

    // Annualized EBITDA
    float annual_tonnes = current_state.throughput_rate * 365.0f;
    float gross_profit_per_tonne = current_state.smelting_margin - current_state.effective_cost;
    current_state.gross_profit = annual_tonnes * gross_profit_per_tonne;
    
    // EBITDA perfectly aligned with SEC data
    current_state.ebitda = current_state.gross_profit - current_state.fixed_costs;

    // EV Calculation
    float risk_penalty = current_state.containment_risk * 5.0f; 
    current_state.adjusted_multiple = std::max(1.0f, current_state.base_multiple - risk_penalty);

    // NEW: Floor at 0. Negative enterprise values break SOTP rollups
    current_state.enterprise_value = std::max(0.0f, current_state.ebitda * current_state.adjusted_multiple);
}

// JSON Packager
json RefineryAsset::get_json_state() const {
    std::lock_guard<std::mutex> lock(asset_mutex);
    return {
        {"asset_id", asset_id},
        {"name", name},
        {"entity_type", entity_type},
        {"enterprise_value", current_state.enterprise_value},
        {"ebitda", current_state.ebitda},
        {"smelting_margin", current_state.smelting_margin},
        {"gross_profit", current_state.gross_profit},
        {"throughput_tonnes", current_state.throughput_rate},
        {"metal_price", current_state.metal_spot_price},
        {"ore_cost", current_state.ore_cost_basis},
        {"recovery_rate", current_state.recovery_rate},
        {"op_health", current_state.op_health},
        {"containment_risk", current_state.containment_risk},
        {"fixed_costs", current_state.fixed_costs},
        {"last_update", current_state.last_update}
    };
}
// ^^^ END FILE: ./thalamus/src/assets/RefineryAsset.cpp ^^^