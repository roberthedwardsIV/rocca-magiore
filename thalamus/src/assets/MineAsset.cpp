#include "MineAsset.hpp"
#include <algorithm>

// Constructor
MineAsset::MineAsset(int id, std::string name) 
    : BaseAsset(id, name, "mine") {
    
    this->process_noise = 0.0005f;
    
    current_state = {
        1.0f, 1.0f, 0.0f,               // Prod, Res, Cost
        1.0f, 0.0f,                     // Op, Threat
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f,   // Uncertainties
        0LL                             // Timestamp
    };
}


// Packet processor -> sends off to apply_signal while locking mutex
void MineAsset::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(asset_mutex);
    apply_signal(sig);
}


// Signal applier -> updates mine state based on new data received + Kalman logic
void MineAsset::apply_signal(const json& sig) {
    current_state.unc_prod += process_noise;
    current_state.unc_cost += process_noise;
    current_state.unc_op += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    // Production rate updates
    if (category == "production") {
        if (sig.contains("value")) {
            float z_prod = sig["value"].get<float>(); 
            float K = current_state.unc_prod / (current_state.unc_prod + R);
            current_state.production_rate += K * (z_prod - current_state.production_rate);
            current_state.unc_prod *= (1.0f - K);
        }
    }
    // Operational Health + Cost Updates
    else if (category == "op") {
        float z = 1.0f - severity;
        float K = current_state.unc_op / (current_state.unc_op + R);
        current_state.op_health += K * (z - current_state.op_health);
        current_state.unc_op *= (1.0f - K);

        if (current_state.op_health < 1.0f) {
             float brokenness = (1.0f - current_state.op_health);
             float strain = current_state.production_rate; 
             
             float inefficiency_penalty = brokenness * strain; 
             
             current_state.cost_per_unit *= (1.0f + inefficiency_penalty);
        }
    } 
    // Financial + Cost Updates
    else if (category == "fin") {
        float z_cost = current_state.cost_per_unit * (1.0f + severity); 
        float K = current_state.unc_cost / (current_state.unc_cost + R);
        current_state.cost_per_unit += K * (z_cost - current_state.cost_per_unit);
        current_state.unc_cost *= (1.0f - K);
    }
    // Geological Updates (deposit-specific)
    else if (category == "geology") {
        float z = 1.0f - severity; 
        float K = current_state.unc_res / (current_state.unc_res + R);
        current_state.proven_reserves += K * (z - current_state.proven_reserves);
        current_state.unc_res *= (1.0f - K);
    }
    
    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json MineAsset::get_json_state() const {
    std::lock_guard<std::mutex> lock(asset_mutex);
    return {
        {"asset_id", asset_id},
        {"name", name},
        {"entity_type", entity_type},
        {"production_rate", current_state.production_rate},
        {"cost_per_unit", current_state.cost_per_unit},
        {"proven_reserves", current_state.proven_reserves},
        {"op_health", current_state.op_health},
        {"threat_level", current_state.threat_level},
        {"last_update", current_state.last_update}
    };
}