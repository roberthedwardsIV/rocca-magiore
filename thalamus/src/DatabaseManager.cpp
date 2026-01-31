#include "DatabaseManager.hpp"
#include <pqxx/pqxx>
#include <iostream>

const std::string conn_str = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";

bool init_database() {
    try {
        pqxx::connection C(conn_str);
        if (C.is_open()) {
            pqxx::work W(C);
            W.exec("SELECT postgis_full_version();");
            W.commit();           
            return true;
        }
    } catch (const std::exception &e) {
        std::cerr << "[DB ERROR] " << e.what() << std::endl;
    }
    return false;
}

void save_to_database(const json& state) {
    try {
        pqxx::connection C(conn_str);
        pqxx::work W(C);
        std::string type = state.value("entity_type", "unknown");
        std::string id;
        if (state.contains("entity_id")) {
            // Earthquakes have string IDs
            id = state["entity_id"].get<std::string>();
        } else if (state.contains("asset_id")) {
            // Assets have integer IDs -> Convert to string
            id = std::to_string(state["asset_id"].get<int>());
        } else if (state.contains("line_id")) {
            // Supply Lines have integer IDs -> Convert to string
            id = std::to_string(state["line_id"].get<int>());
        } else {
            id = "unknown";
        }

        if (type == "earthquake") {
            long long ts = state.value("start_time", 0LL);
            float mag = state.value("final_mag", 0.0f);
            float intensity = state.value("final_intensity", 0.0f);
            float lat = state.value("lat", 0.0f);
            float lon = state.value("lon", 0.0f);
            std::string history = state.value("history_log", json::array()).dump();

            std::string sql = "INSERT INTO earthquakes (id, magnitude, intensity, lat, lon, start_time, history, geom) "
                              "VALUES (" + C.quote(id) + ", " + std::to_string(mag) + ", " + std::to_string(intensity) + ", " +
                              std::to_string(lat) + ", " + std::to_string(lon) + ", " + std::to_string(ts) + ", " +
                              W.quote(history) + "::jsonb, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326)) "
                              "ON CONFLICT (id) DO UPDATE SET magnitude=EXCLUDED.magnitude, intensity=EXCLUDED.intensity, history=EXCLUDED.history;";
            W.exec(sql);
        }
        else if (type == "mine" || type == "refinery") {
            std::string sql = "INSERT INTO asset_states (asset_id, op_health, fin_health, threat_level, last_update) "
                              "VALUES (" + C.quote(id) + ", " + std::to_string(state.value("op_health", 1.0f)) + ", " +
                              std::to_string(state.value("fin_health", 1.0f)) + ", " + std::to_string(state.value("threat_level", 0.0f)) + ", " +
                              std::to_string(state.value("timestamp", 0LL)) + ") "
                              "ON CONFLICT (asset_id) DO UPDATE SET op_health=EXCLUDED.op_health, fin_health=EXCLUDED.fin_health, "
                              "threat_level=EXCLUDED.threat_level, last_update=EXCLUDED.last_update;";
            W.exec(sql);
        }
        else {
            // Generic handler for all supply line types
            std::string sql = "INSERT INTO supply_states (line_id, type, state_data, last_update) "
                              "VALUES (" + C.quote(id) + ", " + W.quote(type) + ", " + W.quote(state.dump()) + "::jsonb, " +
                              std::to_string(state.value("last_update", 0LL)) + ") "
                              "ON CONFLICT (line_id) DO UPDATE SET state_data=EXCLUDED.state_data, last_update=EXCLUDED.last_update;";
            W.exec(sql);
        }
        W.commit();
    } catch (const std::exception &e) {
        std::cerr << "[DB SAVE ERROR] " << e.what() << std::endl;
    }
}

void save_ticker_state(const json& state) {
    try {
        pqxx::connection C(conn_str);
        pqxx::work W(C);

        std::string symbol = state.value("symbol", "UNKNOWN");
        double price = state.value("price", 0.0);
        double vol = state.value("volatility", 0.0);
        
        // Extract Greeks/Metadata into a separate JSONB object
        json extra_data;
        if (state.contains("delta")) extra_data["delta"] = state["delta"];
        if (state.contains("gamma")) extra_data["gamma"] = state["gamma"];
        if (state.contains("iv")) extra_data["iv"] = state["iv"];
        if (state.contains("expiry")) extra_data["expiry"] = state["expiry"];
        if (state.contains("liquidity")) extra_data["liq"] = state["liquidity"];

        std::string sql = "INSERT INTO ticker_states (time_bucket, symbol, price, volatility, greeks) "
                          "VALUES (NOW(), " + W.quote(symbol) + ", " 
                          + std::to_string(price) + ", " 
                          + std::to_string(vol) + ", " 
                          + W.quote(extra_data.dump()) + "::jsonb);";
        
        W.exec(sql);
        W.commit();
    } catch (const std::exception &e) {
        std::cerr << "[DB SAVE ERROR] Ticker save failed: " << e.what() << std::endl;
    }
}

json query_database_for_event(std::string id, float lat, float lon, long long ts, std::string type) {
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        if (type == "earthquake") {
            std::string sql = "SELECT * FROM earthquakes WHERE id = " + N.quote(id) + 
                              " OR (ST_Distance(ST_MakePoint(lon, lat), ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + ")) < 100000 " +
                              " AND ABS(start_time - " + std::to_string(ts) + ") < 180000) LIMIT 1;";
            pqxx::result R = N.exec(sql);
            if (!R.empty()) {
                json j;
                j["entity_id"] = R[0]["id"].as<std::string>();
                j["entity_type"] = "earthquake";
                j["data"] = {{"mag", R[0]["magnitude"].as<float>()}, {"mmi", R[0]["intensity"].as<float>()}, {"lat", R[0]["lat"].as<float>()}, {"lon", R[0]["lon"].as<float>()}};
                j["timestamp"] = R[0]["start_time"].as<long long>();
                return j;
            }
        }
    } catch (...) {}
    return json();
}

std::vector<AssetExposure> check_asset_exposure(float lat, float lon) {
    std::vector<AssetExposure> assets;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        std::string sql = "SELECT id, name, commodity_types[1] as type, latitude, longitude, "
                          "ST_Distance(geom, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326))/1000.0 as d "
                          "FROM assets WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326), sensitivity_radius_km*1000);";
        pqxx::result R = N.exec(sql);
        for (auto row : R) {
            assets.push_back({row["id"].as<int>(), row["name"].as<std::string>(), row["type"].is_null() ? "Unknown" : row["type"].as<std::string>(), row["latitude"].as<float>(), row["longitude"].as<float>(), row["d"].as<float>()});
        }
    } catch (...) {}
    return assets;
}

AssetMetadata get_asset_metadata(int id) {
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        pqxx::result R = N.exec("SELECT name, commodity_types[1] as type FROM assets WHERE id = " + std::to_string(id));
        if (!R.empty()) return {R[0]["name"].as<std::string>(), R[0]["type"].is_null() ? "mine" : R[0]["type"].as<std::string>(), true};
    } catch (...) {}
    return {"Unknown", "mine", false};
}

std::vector<int> get_connected_supply_lines(int asset_id) {
    std::vector<int> ids;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        pqxx::result R = N.exec("SELECT line_id FROM asset_supply_mapping WHERE asset_id = " + std::to_string(asset_id));
        for (auto row : R) ids.push_back(row["line_id"].as<int>());
    } catch (...) {}
    return ids;
}

std::vector<int> get_connected_assets(int line_id) {
    std::vector<int> ids;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        pqxx::result R = N.exec("SELECT asset_id FROM asset_supply_mapping WHERE line_id = " + std::to_string(line_id));
        for (auto row : R) ids.push_back(row["asset_id"].as<int>());
    } catch (...) {}
    return ids;
}

std::vector<SupplyExposure> check_supply_exposure(float lat, float lon) {
    std::vector<SupplyExposure> exposures;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        
        std::string sql = "SELECT line_id, type, dist_km FROM check_supply_exposure(" + 
                          std::to_string(lat) + ", " + std::to_string(lon) + ");";
        
        pqxx::result R = N.exec(sql);
        for (auto row : R) {
            exposures.push_back({
                row["line_id"].as<int>(),
                row["type"].as<std::string>(),
                row["dist_km"].as<float>()
            });
        }
    } catch (const std::exception &e) {
        std::cerr << "[DB ERROR] Supply exposure check failed: " << e.what() << std::endl;
    }
    return exposures;
}