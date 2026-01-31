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

    BaseAsset(int id, std::string name, std::string type) 
        : asset_id(id), name(name), entity_type(type) {}

    virtual ~BaseAsset() = default;

    virtual void process_packet(const json& sig) = 0;
    virtual json get_json_state() const = 0;

protected:
    mutable std::mutex asset_mutex;
};

#endif