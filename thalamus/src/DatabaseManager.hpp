#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

// --- EXPOSURE STRUCTS (Spatial Lookups) ---
struct AssetExposure {
    int asset_id;
    std::string name;
    std::string type;
    float lat;
    float lon;
    float dist_km; 
};

struct AssetMetadata {
    std::string name;
    std::string type;
    bool valid;
};

struct SupplyExposure {
    long long line_id; // Changed to long long for OSM IDs
    std::string type;
    float dist_km;
};

struct ChokePointExposure {
    int cp_id;
    std::string type;
    float dist_km;
};

struct HubExposure {
    long long hub_id;
    std::string type;
    float dist_km;
};

// --- CORE FUNCTIONS ---
bool init_database();
void save_to_database(const json& final_state);
void save_ticker_state(const json& state);

// --- EVENT QUERY ---
json query_database_for_event(std::string id, float lat, float lon, long long timestamp, std::string type);

// --- METADATA LOOKUP ---
AssetMetadata get_asset_metadata(int asset_id);

// --- SPATIAL EXPOSURE (Physics Injection) ---
std::vector<AssetExposure> check_asset_exposure(float lat, float lon);
std::vector<SupplyExposure> check_route_exposure(float lat, float lon);
std::vector<ChokePointExposure> check_chokepoint_exposure(float lat, float lon);

// --- GRAPH TRAVERSAL (Ripple Logic) ---

// 1. ChokePoint <-> Route
std::vector<long long> get_routes_for_chokepoint(int cp_id);
std::vector<int> get_chokepoints_for_route(long long route_id);

// 2. Route <-> Hub
std::vector<long long> get_hubs_for_route(long long route_id);
std::vector<long long> get_routes_for_hub(long long hub_id);

// 3. Hub <-> Asset
std::vector<int> get_assets_for_hub(long long hub_id);
std::vector<long long> get_routes_for_asset(int asset_id);

// 4. Asset <-> Supply Line
std::vector<int> get_connected_supply_lines(int asset_id);
std::vector<int> get_connected_assets(int line_id);

#endif