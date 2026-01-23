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
#include "supply_lines/BaseSupplyLine.hpp"

class GlobalRegistry {
public:
    static std::shared_ptr<BaseEvent> get_event(const std::string& entity_id);
    static std::string find_event_by_proximity(float lat, float lon, long long timestamp, float dist_km = 150.0f, long long time_ms = 180000);
    static void register_event(const std::string& entity_id, std::shared_ptr<BaseEvent> event);
    static void for_each_event(std::function<void(const std::string&, std::shared_ptr<BaseEvent>)> func);
    static void remove_event(const std::string& entity_id);

    static std::shared_ptr<BaseAsset> get_asset(int asset_id);
    static std::shared_ptr<BaseSupplyLine> get_supply_line(int id, const std::string& type = "");
    static void for_each_asset(std::function<void(std::shared_ptr<BaseAsset>)> func);
    static void for_each_supply_line(std::function<void(std::shared_ptr<BaseSupplyLine>)> func);

private:
    static float calculate_distance(float lat1, float lon1, float lat2, float lon2);

    static std::unordered_map<std::string, std::shared_ptr<BaseEvent>> event_map;
    static std::mutex event_mtx;

    static std::unordered_map<int, std::shared_ptr<BaseAsset>> asset_map;
    static std::mutex asset_mtx;

    static std::unordered_map<int, std::shared_ptr<BaseSupplyLine>> supply_map;
    static std::mutex supply_mtx;
};

#endif