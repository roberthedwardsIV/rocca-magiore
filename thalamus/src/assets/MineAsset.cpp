#include "MineAsset.hpp"
#include <algorithm>

MineAsset::MineAsset(int id, std::string name) {
    this->asset_id = id;
    this->name = name;
    this->entity_type = "mine";
    this->process_noise = 0.0005f;
    
    current_state = {1.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0LL};
}

void MineAsset::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(asset_mutex);
    apply_signal(sig);
}

void MineAsset::apply_signal(const json& sig) {
    current_state.unc_op += process_noise;
    current_state.unc_fin += process_noise;
    current_state.unc_threat += process_noise;

    float reliability = sig.value("reliability", 0.5f);
    float R = (1.0f - reliability) + 0.01f;
    std::string category = sig.value("category", "none");
    float severity = sig.value("severity", 0.0f);

    if (category == "op") {
        float z = 1.0f - severity; 
        float K = current_state.unc_op / (current_state.unc_op + R);
        current_state.op_health += K * (z - current_state.op_health);
        current_state.unc_op *= (1.0f - K);
    } 
    else if (category == "fin") {
        float z = 1.0f - severity;
        float K = current_state.unc_fin / (current_state.unc_fin + R);
        current_state.fin_health += K * (z - current_state.fin_health);
        current_state.unc_fin *= (1.0f - K);
    }
    else if (category == "threat") {
        float K = current_state.unc_threat / (current_state.unc_threat + R);
        current_state.threat_level += K * (severity - current_state.threat_level);
        current_state.unc_threat *= (1.0f - K);
    }

    current_state.last_update = sig.value("timestamp", 0LL);
}

json MineAsset::get_json_state() const {
    std::lock_guard<std::mutex> lock(asset_mutex);
    json j;
    j["asset_id"] = asset_id;
    j["name"] = name;
    j["entity_type"] = entity_type;
    j["op_health"] = current_state.op_health;
    j["fin_health"] = current_state.fin_health;
    j["threat_level"] = current_state.threat_level;
    j["last_update"] = current_state.last_update;
    return j;
}