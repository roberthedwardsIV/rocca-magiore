#ifndef PROPAGATOR_HPP
#define PROPAGATOR_HPP

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class Propagator {
public:
    static void propagate_event_impact(const std::string& event_id);
    static void propagate_asset_change(int asset_id);
    static void propagate_supply_change(int line_id);

private:
    static void notify_assets_of_seismic(const std::string& event_id, float event_lat, float event_lon);
    static void notify_supply_of_seismic(const std::string& event_id, float event_lat, float event_lon);
};

#endif