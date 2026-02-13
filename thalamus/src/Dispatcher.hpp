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
    static void handle_ticker_signal(const json& sig);

    static void handle_route_signal(const json& sig);     
    static void handle_hub_signal(const json& sig);       
    static void handle_chokepoint_signal(const json& sig);

    static void trigger_twitter_recon(const std::string& id, const std::string& type, float lat, float lon, long long ts);
};

#endif