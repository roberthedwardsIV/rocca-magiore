#ifndef GLOBAL_REGISTRY_HPP
#define GLOBAL_REGISTRY_HPP

#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <functional>
#include <cmath>

#include "events/BaseEvent.hpp"
#include "assets/BaseAsset.hpp"
#include "routes/BaseRoute.hpp"
#include "hubs/BaseHub.hpp"
#include "chokepoints/BaseChokePoint.hpp"

class GlobalRegistry {
public:
    static std::shared_ptr<BaseEvent> get_event(const std::string& entity_id);
    static std::string find_event_by_proximity(float lat, float lon, long long timestamp, const std::string& entity_type);
    static void register_event(const std::string& entity_id, std::shared_ptr<BaseEvent> event);
    static void for_each_event(std::function<void(const std::string&, std::shared_ptr<BaseEvent>)> func);
    static void remove_event(const std::string& entity_id);

    static std::shared_ptr<BaseAsset> get_asset(int asset_id);
    static void for_each_asset(std::function<void(std::shared_ptr<BaseAsset>)> func);

    // Factory Method for Routes (e.g. "rail_line" -> RailRoute)
    static std::shared_ptr<BaseRoute> get_route(long long id, const std::string& type = "");
    static void for_each_route(std::function<void(std::shared_ptr<BaseRoute>)> func);

    // Factory Method for Hubs (e.g. "port" -> PortHub)
    static std::shared_ptr<BaseHub> get_hub(long long id, const std::string& type = "");
    static void for_each_hub(std::function<void(std::shared_ptr<BaseHub>)> func);

    // Factory Method for ChokePoints (e.g. "bridge" -> BridgeChokePoint)
    static std::shared_ptr<BaseChokePoint> get_chokepoint(int id, const std::string& type = "");
    static void for_each_chokepoint(std::function<void(std::shared_ptr<BaseChokePoint>)> func);

private:
    static float calculate_distance(float lat1, float lon1, float lat2, float lon2);

    static std::unordered_map<std::string, std::shared_ptr<BaseEvent>> event_map;
    static std::mutex event_mtx;

    static std::unordered_map<int, std::shared_ptr<BaseAsset>> asset_map;
    static std::mutex asset_mtx;

    static std::unordered_map<long long, std::shared_ptr<BaseRoute>> route_map;
    static std::mutex route_mtx;

    static std::unordered_map<long long, std::shared_ptr<BaseHub>> hub_map;
    static std::mutex hub_mtx;

    static std::unordered_map<int, std::shared_ptr<BaseChokePoint>> chokepoint_map;
    static std::mutex chokepoint_mtx;
};

#endif