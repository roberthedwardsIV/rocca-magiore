/**
* GlobalRegistry.cpp: mainly helper functions used by the Dispatcher + Propagator. Also
*                     maintains the registries for events, assets and supply lines
*                     that get updated as raw_signals pour in.
 */
#include "GlobalRegistry.hpp"
#include "DatabaseManager.hpp"

// Event Subclasses
#include "events/EarthquakeTracker.hpp"

// Asset Subclasses
#include "assets/MineAsset.hpp"
#include "assets/RefineryAsset.hpp"

// Supply Line Subclasses
#include "supply_lines/RailLine.hpp"
#include "supply_lines/RailYard.hpp"
#include "supply_lines/MaritimeRoute.hpp"
#include "supply_lines/MaritimePort.hpp"
#include "supply_lines/PipelineLine.hpp"
#include "supply_lines/PipelineStation.hpp"
#include "supply_lines/Highway.hpp"
#include "supply_lines/Airport.hpp"
#include "supply_lines/Airspace.hpp"
#include "supply_lines/CanalRoute.hpp"
#include "supply_lines/CanalLock.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Registry definitions (events, assets + supply assets)
std::unordered_map<std::string, std::shared_ptr<BaseEvent>> GlobalRegistry::event_map;
std::mutex GlobalRegistry::event_mtx;
std::unordered_map<int, std::shared_ptr<BaseAsset>> GlobalRegistry::asset_map;
std::mutex GlobalRegistry::asset_mtx;
std::unordered_map<int, std::shared_ptr<BaseSupplyLine>> GlobalRegistry::supply_map;
std::mutex GlobalRegistry::supply_mtx;


// Helper function: haversine distance calculation
float GlobalRegistry::calculate_distance(float lat1, float lon1, float lat2, float lon2) {
    const float EARTH_RADIUS_KM = 6371.0f;
    float dLat = (lat2 - lat1) * M_PI / 180.0f;
    float dLon = (lon2 - lon1) * M_PI / 180.0f;

    float a = sin(dLat / 2) * sin(dLat / 2) +
              cos(lat1 * M_PI / 180.0f) * cos(lat2 * M_PI / 180.0f) *
              sin(dLon / 2) * sin(dLon / 2);
    
    float c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return EARTH_RADIUS_KM * c;
}

// EVENT LOGIC
// Searches for events by direct ID matches 
std::shared_ptr<BaseEvent> GlobalRegistry::get_event(const std::string& id) {
    std::lock_guard<std::mutex> lock(event_mtx);
    auto it = event_map.find(id);
    return (it != event_map.end()) ? it->second : nullptr;
}


// Searches for events in proximity (uses calculate_distance() as needed)
std::string GlobalRegistry::find_event_by_proximity(float lat, float lon, long long timestamp, const std::string& entity_type) {
    float radius_km = 100.0f;       // 100 km radius
    long long time_ms = 180000LL;   // 3 min. timeframe

    std::lock_guard<std::mutex> lock(event_mtx);

    for (auto const& [id, event] : event_map) {
        std::string type = event->entity_type; // verify event types match
        if (type != entity_type) continue;

        float d = calculate_distance(lat, lon, event->get_lat(), event->get_lon());
        if (d > radius_km) continue;

        long long t_diff = std::abs(timestamp - event->get_start_time());
        if (t_diff > time_ms) continue;

        return id;
    }
    return "";
}


// Adds new event to event registry
void GlobalRegistry::register_event(const std::string& id, std::shared_ptr<BaseEvent> event) {
    std::lock_guard<std::mutex> lock(event_mtx);
    event_map[id] = event;
}


// Method to iterate through all events in event registry (thread-safe) without individual mutex locks/unlocks for each one
void GlobalRegistry::for_each_event(std::function<void(const std::string&, std::shared_ptr<BaseEvent>)> func) {
    std::lock_guard<std::mutex> lock(event_mtx);
    for (auto& [id, event] : event_map) {
        func(id, event);
    }
}


// Method to remove events from registry when stale + archived
void GlobalRegistry::remove_event(const std::string& id) {
    std::lock_guard<std::mutex> lock(event_mtx);
    event_map.erase(id);
}


// ASSET LOGIC
// Searches for existing or creates new asset state class using helpers from DatabaseManager.cpp
std::shared_ptr<BaseAsset> GlobalRegistry::get_asset(int id) {
    std::lock_guard<std::mutex> lock(asset_mtx);
    if (asset_map.count(id)) return asset_map[id];

    AssetMetadata meta = get_asset_metadata(id); // returns asset name + type if found (defaults to Unknown mine)
    if (!meta.valid) return nullptr;

    std::shared_ptr<BaseAsset> asset;
    if (meta.type == "mine") {
        asset = std::make_shared<MineAsset>(id, meta.name);
    } else if (meta.type == "refinery") {
        asset = std::make_shared<RefineryAsset>(id, meta.name);
    }
    
    if (asset) asset_map[id] = asset;
    return asset;
}


// Method to iterate through all assets in asset registry (thread-safe) without individual mutex locks/unlocks for each one
void GlobalRegistry::for_each_asset(std::function<void(std::shared_ptr<BaseAsset>)> func) {
    std::lock_guard<std::mutex> lock(asset_mtx);
    for (auto& [id, asset] : asset_map) {
        func(asset);
    }
}


// Searches for existing or creates new supply_line state class as applicable
std::shared_ptr<BaseSupplyLine> GlobalRegistry::get_supply_line(int id, const std::string& type) {
    std::lock_guard<std::mutex> lock(supply_mtx);
    if (supply_map.count(id)) return supply_map[id];

    std::shared_ptr<BaseSupplyLine> line;
    if (type == "rail_line") line = std::make_shared<RailLine>(id, "RailLine_" + std::to_string(id));
    else if (type == "rail_yard") line = std::make_shared<RailYard>(id, "RailYard_" + std::to_string(id));
    else if (type == "maritime_route") line = std::make_shared<MaritimeRoute>(id, "MaritimeRoute_" + std::to_string(id));
    else if (type == "maritime_port") line = std::make_shared<MaritimePort>(id, "MaritimePort_" + std::to_string(id));
    else if (type == "pipeline_line") line = std::make_shared<PipelineLine>(id, "PipelineLine_" + std::to_string(id));
    else if (type == "pipeline_station") line = std::make_shared<PipelineStation>(id, "PipelineStation_" + std::to_string(id));
    else if (type == "highway") line = std::make_shared<Highway>(id, "Highway_" + std::to_string(id));
    else if (type == "airport") line = std::make_shared<Airport>(id, "Airport_" + std::to_string(id));
    else if (type == "airspace") line = std::make_shared<Airspace>(id, "Airspace_" + std::to_string(id));
    else if (type == "canal_route") line = std::make_shared<CanalRoute>(id, "CanalRoute_" + std::to_string(id));
    else if (type == "canal_lock") line = std::make_shared<CanalLock>(id, "CanalLock_" + std::to_string(id));

    if (line) supply_map[id] = line;
    return line;
}


// Method to iterate through all supply lines in supply registry (thread-safe) without individual mutex locks/unlocks for each one
void GlobalRegistry::for_each_supply_line(std::function<void(std::shared_ptr<BaseSupplyLine>)> func) {
    std::lock_guard<std::mutex> lock(supply_mtx);
    for (auto& [id, line] : supply_map) {
        func(line);
    }
}