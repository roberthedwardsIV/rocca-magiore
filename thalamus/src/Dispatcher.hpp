#ifndef DISPATCHER_HPP
#define DISPATCHER_HPP

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class Dispatcher {
public:
    static void route_signal(const json& sig);

private:
    static void handle_event_signal(const json& sig);
    static void handle_asset_signal(const json& sig);
    static void handle_supply_signal(const json& sig);
    
    static std::string extract_station_prefix(const std::string& entity_id);
};

#endif