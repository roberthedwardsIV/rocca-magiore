#ifndef BASE_SUPPLY_LINE_HPP
#define BASE_SUPPLY_LINE_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <mutex>

using json = nlohmann::json;

class BaseSupplyLine {
public:
    int line_id;
    std::string name;
    std::string entity_type;
    std::string& line_type = entity_type;
    float integrity;
    float flow_capacity;

    BaseSupplyLine() : line_id(-1), integrity(1.0f), flow_capacity(1.0f) {}
    BaseSupplyLine(int id, std::string name, std::string type) 
        : line_id(id), name(name), entity_type(type), integrity(1.0f), flow_capacity(1.0f) {}

    virtual ~BaseSupplyLine() = default;

    virtual void process_packet(const json& sig) {
        std::lock_guard<std::mutex> lock(supply_mutex);
        if (sig.contains("severity")) {
            this->integrity -= (sig["severity"].get<float>() * 0.1f);
        }
    }

    virtual json get_json_state() const {
        std::lock_guard<std::mutex> lock(supply_mutex);
        return {
            {"line_id", line_id},
            {"name", name},
            {"entity_type", entity_type},
            {"integrity", integrity},
            {"flow_capacity", flow_capacity}
        };
    }

protected:
    mutable std::mutex supply_mutex;
};

#define line_mutex supply_mutex

#endif