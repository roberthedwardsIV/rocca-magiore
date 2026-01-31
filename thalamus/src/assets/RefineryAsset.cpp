#include "RefineryAsset.hpp"
#include <algorithm>
#include <cmath>

// Constructor
RefineryAsset::RefineryAsset(int id, std::string name) 
    : BaseAsset(id, name, "refinery") {
    
    this->process_noise = 0.002f; 
    
    current_state = {
        0.9f, 0.5f, 
        5.0f, 
        1.0f, 0.0f,
        0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        0LL
    };
}


// Packet Processor -> sends off to apply_signal() with mutex locked
void RefineryAsset::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(asset_mutex);
    apply_signal(sig);
}


// Signal applier -> updates refinery state based on new data received + Kalman logic
void RefineryAsset::apply_signal(const json& sig) {
    current_state.unc_thru += process_noise;
    current_state.unc_store += process_noise;
    current_state.unc_cost += process_noise;
    current_state.unc_op += process_noise;
    current_state.unc_risk += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    // Processing Rate Updates
    if (category == "throughput") {
        if (sig.contains("value")) {
            // KALMAN UPDATE for Throughput
            float z_thru = sig["value"].get<float>();
            float K = current_state.unc_thru / (current_state.unc_thru + R);
            current_state.throughput_rate += K * (z_thru - current_state.throughput_rate);
            current_state.unc_thru *= (1.0f - K);
        }
    }
    // Operational + Cost Updates
    else if (category == "op") {
        float z = 1.0f - severity;
        float K = current_state.unc_op / (current_state.unc_op + R);
        current_state.op_health += K * (z - current_state.op_health);
        current_state.unc_op *= (1.0f - K);

        if (current_state.op_health < 1.0f) {
             float brokenness = (1.0f - current_state.op_health);
             float strain = current_state.throughput_rate;
             
             float inefficiency_penalty = brokenness * strain; 
             current_state.refining_cost *= (1.0f + inefficiency_penalty);
             
             current_state.containment_risk += (inefficiency_penalty * 0.1f);
        }
    } 
    // Financial + Cost Updates
    else if (category == "fin") {
        float z_cost = current_state.refining_cost * (1.0f + severity); 
        float K = current_state.unc_cost / (current_state.unc_cost + R);
        current_state.refining_cost += K * (z_cost - current_state.refining_cost);
        current_state.unc_cost *= (1.0f - K);
    }
    // External Threat Updates
    else if (category == "threat") {
        float K = current_state.unc_risk / (current_state.unc_risk + R);
        current_state.containment_risk += K * (severity - current_state.containment_risk);
        current_state.unc_risk *= (1.0f - K);
    }
    else if (category == "logistics") {
        // Storage issues (inability to offload product)
        if (sig.contains("value")) {
            float z_store = sig["value"].get<float>();
            float K = current_state.unc_store / (current_state.unc_store + R);
            current_state.storage_level += K * (z_store - current_state.storage_level);
            current_state.unc_store *= (1.0f - K);
            
            // High storage levels increase cost (demurrage)
            if (current_state.storage_level > 0.9f) {
                current_state.refining_cost *= 1.05f; 
            }
        }
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}


// JSON packager for archiving state history to db
json RefineryAsset::get_json_state() const {
    std::lock_guard<std::mutex> lock(asset_mutex);
    return {
        {"asset_id", asset_id},
        {"name", name},
        {"entity_type", entity_type},
        {"throughput_rate", current_state.throughput_rate},
        {"storage_level", current_state.storage_level},
        {"refining_cost", current_state.refining_cost},
        {"op_health", current_state.op_health},
        {"containment_risk", current_state.containment_risk},
        {"last_update", current_state.last_update}
    };
}