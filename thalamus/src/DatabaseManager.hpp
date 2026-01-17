#ifndef DATABASE_MANAGER_HPP
#define DATABASE_MANAGER_HPP

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

bool init_database();

void save_to_database(const json& final_state);

json query_database_for_event(std::string id, float lat, float lon, long long timestamp);

#endif