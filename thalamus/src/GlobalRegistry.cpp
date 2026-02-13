/**
* GlobalRegistry.cpp: mainly helper functions used by the Dispatcher + Propagator. Also
*                     maintains the registries for events, assets and supply lines
*                     that get updated as raw_signals pour in.
 */
#include "GlobalRegistry.hpp"
#include "DatabaseManager.hpp"

// Event Subclasses
#include "events/EarthquakeTracker.hpp"
#include "events/WildfireTracker.hpp"

// Asset Subclasses
#include "assets/MineAsset.hpp"
#include "assets/RefineryAsset.hpp"
#include "assets/SmelterAsset.hpp"

// Route Subclasses
#include "routes/RailRoute.hpp"
#include "routes/RoadRoute.hpp"
#include "routes/MaritimeRoute.hpp"
#include "routes/AirRoute.hpp"
#include "routes/PipelineRoute.hpp"
#include "routes/PowerTransmissionRoute.hpp"
#include "routes/WaterwayRoute.hpp"

// Hub Subclasses
#include "hubs/PortHub.hpp"
#include "hubs/AirportHub.hpp"
#include "hubs/RailNodeHub.hpp"
#include "hubs/DistributionHub.hpp"
#include "hubs/PowerSubstationHub.hpp"
#include "hubs/PowerPlantHub.hpp"

// Chokepoint Subclasses
#include "chokepoints/BridgeChokePoint.hpp"
#include "chokepoints/TunnelChokePoint.hpp"
#include "chokepoints/BorderChokePoint.hpp"
#include "chokepoints/CanalLockChokePoint.hpp"
#include "chokepoints/DamChokePoint.hpp"
#include "chokepoints/RunwayChokePoint.hpp"
#include "chokepoints/PowerSubstationChokePoint.hpp"
#include "chokepoints/PortCraneChokePoint.hpp"
#include "chokepoints/PumpingStationChokePoint.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Registry definitions (events, assets + supply assets)
std::unordered_map<std::string, std::shared_ptr<BaseEvent>> GlobalRegistry::event_map;
std::mutex GlobalRegistry::event_mtx;

std::unordered_map<int, std::shared_ptr<BaseAsset>> GlobalRegistry::asset_map;
std::mutex GlobalRegistry::asset_mtx;

std::unordered_map<long long, std::shared_ptr<BaseRoute>> GlobalRegistry::route_map;
std::mutex GlobalRegistry::route_mtx;

std::unordered_map<long long, std::shared_ptr<BaseHub>> GlobalRegistry::hub_map;
std::mutex GlobalRegistry::hub_mtx;

std::unordered_map<int, std::shared_ptr<BaseChokePoint>> GlobalRegistry::chokepoint_map;
std::mutex GlobalRegistry::chokepoint_mtx;


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
    if (meta.type == "mine") asset = std::make_shared<MineAsset>(id, meta.name);
    else if (meta.type == "refinery") asset = std::make_shared<RefineryAsset>(id, meta.name);
    else if (meta.type == "smelter") asset = std::make_shared<SmelterAsset>(id, meta.name);
    
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


// --- ROUTE LOGIC (Factories) ---
std::shared_ptr<BaseRoute> GlobalRegistry::get_route(long long id, const std::string& type) {
    std::lock_guard<std::mutex> lock(route_mtx);
    if (route_map.count(id)) return route_map[id];

    // Lazy instantiation factory
    std::shared_ptr<BaseRoute> route;
    std::string name = "Route_" + std::to_string(id);

    if (type == "rail_line" || type == "rail_mainline") 
        route = std::make_shared<RailRoute>(id, name);
    else if (type == "maritime_route") 
        route = std::make_shared<MaritimeRoute>(id, name);
    else if (type == "pipeline" || type == "pipeline_line") 
        route = std::make_shared<PipelineRoute>(id, name);
    else if (type == "highway" || type == "highway_trunk" || type == "road") 
        route = std::make_shared<RoadRoute>(id, name);
    else if (type == "air_route" || type == "air") 
        route = std::make_shared<AirRoute>(id, name);
    else if (type == "waterway" || type == "inland_waterway") 
        route = std::make_shared<WaterwayRoute>(id, name);
    else if (type == "power_line" || type == "power_grid") 
        route = std::make_shared<PowerTransmissionRoute>(id, name);

    if (route) route_map[id] = route;
    return route;
}

void GlobalRegistry::for_each_route(std::function<void(std::shared_ptr<BaseRoute>)> func) {
    std::lock_guard<std::mutex> lock(route_mtx);
    for (auto& [id, route] : route_map) func(route);
}

// --- HUB LOGIC (Factories) ---
std::shared_ptr<BaseHub> GlobalRegistry::get_hub(long long id, const std::string& type) {
    std::lock_guard<std::mutex> lock(hub_mtx);
    if (hub_map.count(id)) return hub_map[id];

    std::shared_ptr<BaseHub> hub;
    std::string name = "Hub_" + std::to_string(id);
    // Placeholder Lat/Lon (0.0) -> Should ideally be passed in or fetched via DB if not present
    double lat = 0.0, lon = 0.0; 

    if (type == "port" || type == "maritime_port") 
        hub = std::make_shared<PortHub>(id, name, lat, lon);
    else if (type == "airport" || type == "aerodrome") 
        hub = std::make_shared<AirportHub>(id, name, lat, lon);
    else if (type == "rail_node" || type == "marshalling_yard") 
        hub = std::make_shared<RailNodeHub>(id, name, lat, lon);
    else if (type == "warehouse" || type == "logistics_terminal" || type == "storage_tank") 
        hub = std::make_shared<DistributionHub>(id, name, lat, lon);
    else if (type == "substation") 
        hub = std::make_shared<PowerSubstation>(id, name, lat, lon);
    else if (type == "power_plant") 
        hub = std::make_shared<PowerPlantHub>(id, name, lat, lon);

    if (hub) hub_map[id] = hub;
    return hub;
}

void GlobalRegistry::for_each_hub(std::function<void(std::shared_ptr<BaseHub>)> func) {
    std::lock_guard<std::mutex> lock(hub_mtx);
    for (auto& [id, hub] : hub_map) func(hub);
}

// --- CHOKEPOINT LOGIC (Factories) ---
std::shared_ptr<BaseChokePoint> GlobalRegistry::get_chokepoint(int id, const std::string& type) {
    std::lock_guard<std::mutex> lock(chokepoint_mtx);
    if (chokepoint_map.count(id)) return chokepoint_map[id];

    std::shared_ptr<BaseChokePoint> cp;
    std::string name = "CP_" + std::to_string(id);
    double lat = 0.0, lon = 0.0; // Placeholders

    if (type == "bridge") 
        cp = std::make_shared<BridgeChokePoint>(id, name, lat, lon, 100.0f, 500.0f, 15.0f);
    else if (type == "tunnel") 
        cp = std::make_shared<TunnelChokePoint>(id, name, lat, lon, 2000.0f);
    else if (type == "border" || type == "border_crossing") 
        cp = std::make_shared<BorderChokePoint>(id, name, lat, lon, "UNK", "UNK");
    else if (type == "canal_lock") 
        cp = std::make_shared<CanalLockChokePoint>(id, name, lat, lon, 12.0f, 33.0f);
    else if (type == "dam") 
        cp = std::make_shared<DamChokePoint>(id, name, lat, lon, 1000000.0f);
    else if (type == "runway") 
        cp = std::make_shared<RunwayChokePoint>(id, name, lat, lon, 3000.0f, 45.0f, "asphalt");
    else if (type == "pumping_station") 
        cp = std::make_shared<PumpingStationChokePoint>(id, name, lat, lon, 1000.0f, 50000.0f);
    else if (type == "crane") 
        cp = std::make_shared<PortCraneChokePoint>(id, name, lat, lon, 60.0f, 40.0f);
    else if (type == "substation_cp") 
        cp = std::make_shared<PowerSubstationChokePoint>(id, name, lat, lon, 220.0f, 200.0f);

    if (cp) chokepoint_map[id] = cp;
    return cp;
}

void GlobalRegistry::for_each_chokepoint(std::function<void(std::shared_ptr<BaseChokePoint>)> func) {
    std::lock_guard<std::mutex> lock(chokepoint_mtx);
    for (auto& [id, cp] : chokepoint_map) func(cp);
}