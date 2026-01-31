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

    BaseSupplyLine(int id, std::string name, std::string type) 
        : line_id(id), name(name), entity_type(type) {}

    virtual ~BaseSupplyLine() = default;

    virtual void process_packet(const json& sig) = 0;
    virtual json get_json_state() const = 0;

protected:
    mutable std::mutex supply_mutex;
};

#endif