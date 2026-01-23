#include "RefineryAsset.hpp"
#include <algorithm>

RefineryAsset::RefineryAsset(int id, std::string name) {
    this->asset_id = id;
    this->name = name;
    this->entity_type = "refinery";
    
    this->process_noise = 0.002f; 
    
    current_state = {1.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0LL};
}

void RefineryAsset::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(asset_mutex);
    apply_signal(sig);
}

void RefineryAsset::apply_signal(const json& sig) {
    current_state.unc_op += process_noise;
    current_state.unc_risk += process_noise;
    current_state.unc_fin += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "op") {
        float z = 1.0f - severity;
        float K = current_state.unc_op / (current_state.unc_op + R);
        current_state.op_health += K * (z - current_state.op_health);
        current_state.unc_op *= (1.0f - K);

        // Significant Op hits (accidents) spike containment risk
        if (severity > 0.6f) {
            current_state.containment_risk = std::max(current_state.containment_risk, severity);
            current_state.unc_risk = 0.1f; 
        }
    } 
    else if (category == "threat") {
        // External threats (ex: Fire/Storm) update the internal containment risk
        float K = current_state.unc_risk / (current_state.unc_risk + R);
        current_state.containment_risk += K * (severity - current_state.containment_risk);
        current_state.unc_risk *= (1.0f - K);
    }
    else if (category == "fin") {
        float z = 1.0f - severity;
        float K = current_state.unc_fin / (current_state.unc_fin + R);
        current_state.fin_health += K * (z - current_state.fin_health);
        current_state.unc_fin *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json RefineryAsset::get_json_state() const {
    std::lock_guard<std::mutex> lock(asset_mutex);
    json j;
    j["asset_id"] = asset_id;
    j["name"] = name;
    j["entity_type"] = entity_type;
    j["op_health"] = current_state.op_health;
    j["containment_risk"] = current_state.containment_risk;
    j["fin_health"] = current_state.fin_health;
    j["last_update"] = current_state.last_update;
    return j;
}