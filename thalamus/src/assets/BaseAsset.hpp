#ifndef BASE_ASSET_HPP
#define BASE_ASSET_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <mutex>

using json = nlohmann::json;

class BaseAsset {
public:
    int asset_id;
    std::string name;
    std::string entity_type;
    float op_health;
    float fin_health;
    BaseAsset() : asset_id(-1), op_health(1.0f), fin_health(1.0f) {}

    BaseAsset(int id, std::string name, std::string type) 
        : asset_id(id), name(name), entity_type(type), op_health(1.0f), fin_health(1.0f) {}

    virtual ~BaseAsset() = default;

    virtual void process_packet(const json& sig) {
        std::lock_guard<std::mutex> lock(asset_mutex);
        if (sig.contains("severity")) {
            this->op_health -= (sig["severity"].get<float>() * 0.1f);
        }
    }

    virtual json get_json_state() const {
        std::lock_guard<std::mutex> lock(asset_mutex);
        return {
            {"asset_id", asset_id},
            {"name", name},
            {"entity_type", entity_type},
            {"op_health", op_health},
            {"fin_health", fin_health}
        };
    }

protected:
    mutable std::mutex asset_mutex;
};

#define line_mutex asset_mutex


#endif