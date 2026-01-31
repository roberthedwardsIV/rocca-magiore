#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

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
    int line_id;
    std::string type;
    float dist_km;
};

bool init_database();

void save_to_database(const json& final_state);
void save_ticker_state(const json& state);

json query_database_for_event(std::string id, float lat, float lon, long long timestamp, std::string type);
AssetMetadata get_asset_metadata(int asset_id);

std::vector<AssetExposure> check_asset_exposure(float event_lat, float event_lon);
std::vector<SupplyExposure> check_supply_exposure(float lat, float lon);
std::vector<int> get_connected_supply_lines(int asset_id);
std::vector<int> get_connected_assets(int line_id);

#endif