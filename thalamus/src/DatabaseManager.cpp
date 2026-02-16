#include "DatabaseManager.hpp"
#include <pqxx/pqxx>
#include <iostream>

// Configuration (Should match Docker env)
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
            id = std::to_string(state["line_id"].get<long long>());
        } else if (state.contains("cp_id")) {
            // Chokepoints have integer IDs
            id = std::to_string(state["cp_id"].get<int>());
        } else if (state.contains("hub_id")) {
            // Hubs have integer IDs
            id = std::to_string(state["hub_id"].get<long long>());
        } else if (state.contains("id")) {
            // Fallback for generic objects
            if (state["id"].is_number()) id = std::to_string(state["id"].get<long long>());
            else id = state["id"].get<std::string>();
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
        else if (type == "mine" || type == "refinery" || type == "smelter" || type == "power_plant") {
            std::string sql = "INSERT INTO asset_states (asset_id, op_health, fin_health, threat_level, last_update) "
                              "VALUES (" + C.quote(id) + ", " + std::to_string(state.value("op_health", 1.0f)) + ", " +
                              std::to_string(state.value("fin_health", 1.0f)) + ", " + std::to_string(state.value("threat_level", 0.0f)) + ", " +
                              std::to_string(state.value("timestamp", 0LL)) + ") "
                              "ON CONFLICT (asset_id) DO UPDATE SET op_health=EXCLUDED.op_health, fin_health=EXCLUDED.fin_health, "
                              "threat_level=EXCLUDED.threat_level, last_update=EXCLUDED.last_update;";
            W.exec(sql);
        }
        else {
            // Generic handler for supply lines, hubs, chokepoints
            // Note: In a real system, you might want separate tables for hubs/chokepoints history
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
        double price = state.value("market_price", state.value("price", 0.0));
        double vol = state.value("market_volatility", state.value("volatility", 0.0));
        
        // Extract Greeks/Metadata into a separate JSONB object
        json extra_data;
        if (state.contains("delta")) extra_data["delta"] = state["delta"];
        if (state.contains("gamma")) extra_data["gamma"] = state["gamma"];
        if (state.contains("iv")) extra_data["iv"] = state["iv"];
        if (state.contains("expiry")) extra_data["expiry"] = state["expiry"];
        if (state.contains("liquidity")) extra_data["liq"] = state["liquidity"];
        if (state.contains("fair_value")) extra_data["fv"] = state["fair_value"];

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
        // Explicit mapping table + Spatial Proximity Fallback
        pqxx::result R = N.exec("SELECT line_id FROM asset_supply_mapping WHERE asset_id = " + std::to_string(asset_id));
        for (auto row : R) ids.push_back(row["line_id"].as<int>());
        
        // If no explicit links, find routes starting near the asset
        if (ids.empty()) {
             std::vector<long long> routes = get_routes_for_asset(asset_id);
             for(long long rid : routes) ids.push_back((int)rid);
        }
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

// --- SPATIAL EXPOSURE CHECKERS ---

std::vector<AssetExposure> check_asset_exposure(float lat, float lon) {
    std::vector<AssetExposure> results;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        
        // Find Assets within 50km (0.5 degrees roughly) of event
        std::string sql = 
            "SELECT id, name, commodity_types[1] as type, latitude, longitude, "
            "ST_Distance(geom::geography, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326)::geography) / 1000.0 as dist_km "
            "FROM assets "
            "WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326), 0.5);";
            
        pqxx::result R = N.exec(sql);
        for (auto row : R) {
            results.push_back({
                row["id"].as<int>(),
                row["name"].as<std::string>(),
                row["type"].is_null() ? "unknown" : row["type"].as<std::string>(),
                row["latitude"].as<float>(),
                row["longitude"].as<float>(),
                row["dist_km"].as<float>()
            });
        }
    } catch (...) {}
    return results;
}

std::vector<SupplyExposure> check_route_exposure(float lat, float lon) {
    std::vector<SupplyExposure> results;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        
        // Find Routes within 20km (0.2 degrees) - Tighter radius for lines
        std::string sql = 
            "SELECT line_id, type, "
            "ST_Distance(geom::geography, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326)::geography) / 1000.0 as dist_km "
            "FROM supply_lines "
            "WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326), 0.2);";

        pqxx::result R = N.exec(sql);
        for (auto row : R) {
            results.push_back({
                row["line_id"].as<long long>(),
                row["type"].as<std::string>(),
                row["dist_km"].as<float>()
            });
        }
    } catch (...) {}
    return results;
}

std::vector<ChokePointExposure> check_chokepoint_exposure(float lat, float lon) {
    std::vector<ChokePointExposure> results;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        
        // Find Bridges/Dams within 50km
        std::string sql = 
            "SELECT id, type, "
            "ST_Distance(geom::geography, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326)::geography) / 1000.0 as dist_km "
            "FROM supply_chokepoints "
            "WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + "," + std::to_string(lat) + "), 4326), 0.5);";

        pqxx::result R = N.exec(sql);
        for (auto row : R) {
            results.push_back({
                row["id"].as<int>(),
                row["type"].as<std::string>(),
                row["dist_km"].as<float>()
            });
        }
    } catch (...) {}
    return results;
}

// --- GRAPH TRAVERSAL (PHYSICS JOINS) ---

// 1. ChokePoint <-> Route
std::vector<long long> get_routes_for_chokepoint(int cp_id) {
    std::vector<long long> routes;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        // Find routes that intersect the chokepoint (Buffer 20m to account for GPS drift)
        std::string sql = 
            "SELECT l.line_id FROM supply_lines l "
            "JOIN supply_chokepoints c ON ST_DWithin(l.geom, c.geom, 0.0002) " // ~20m buffer
            "WHERE c.id = " + std::to_string(cp_id);
        
        pqxx::result R = N.exec(sql);
        for (auto row : R) routes.push_back(row[0].as<long long>());
    } catch (...) {}
    return routes;
}

std::vector<int> get_chokepoints_for_route(long long route_id) {
    std::vector<int> cps;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        std::string sql = 
            "SELECT c.id FROM supply_chokepoints c "
            "JOIN supply_lines l ON ST_DWithin(c.geom, l.geom, 0.0002) "
            "WHERE l.line_id = " + std::to_string(route_id);
        
        pqxx::result R = N.exec(sql);
        for (auto row : R) cps.push_back(row[0].as<int>());
    } catch (...) {}
    return cps;
}

// 2. Route <-> Hub
std::vector<long long> get_hubs_for_route(long long route_id) {
    std::vector<long long> hubs;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        // Hubs within 1km of route endpoints or path
        std::string sql = 
            "SELECT h.id FROM supply_hubs h "
            "JOIN supply_lines l ON ST_DWithin(h.geom, l.geom, 0.01) " // ~1km
            "WHERE l.line_id = " + std::to_string(route_id);
        
        pqxx::result R = N.exec(sql);
        for (auto row : R) hubs.push_back(row[0].as<long long>());
    } catch (...) {}
    return hubs;
}

std::vector<long long> get_routes_for_hub(long long hub_id) {
    std::vector<long long> routes;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        std::string sql = 
            "SELECT l.line_id FROM supply_lines l "
            "JOIN supply_hubs h ON ST_DWithin(l.geom, h.geom, 0.01) "
            "WHERE h.id = " + std::to_string(hub_id);
        
        pqxx::result R = N.exec(sql);
        for (auto row : R) routes.push_back(row[0].as<long long>());
    } catch (...) {}
    return routes;
}

// 3. Hub <-> Asset
std::vector<int> get_assets_for_hub(long long hub_id) {
    std::vector<int> assets;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        // Find Assets that likely use this Hub (Proximity < 50km)
        // OR explicit link in asset_supply_mapping table (if populated by python)
        std::string sql = 
            "SELECT a.id FROM assets a "
            "JOIN supply_hubs h ON ST_DWithin(a.geom, h.geom, 0.5) " // ~50km
            "WHERE h.id = " + std::to_string(hub_id);
        
        pqxx::result R = N.exec(sql);
        for (auto row : R) assets.push_back(row[0].as<int>());
    } catch (...) {}
    return assets;
}

std::vector<long long> get_routes_for_asset(int asset_id) {
    std::vector<long long> routes;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        // Routes starting near the asset (< 2km)
        std::string sql = 
            "SELECT l.line_id FROM supply_lines l "
            "JOIN assets a ON ST_DWithin(l.geom, a.geom, 0.02) " // ~2km
            "WHERE a.id = " + std::to_string(asset_id);
        
        pqxx::result R = N.exec(sql);
        for (auto row : R) routes.push_back(row[0].as<long long>());
    } catch (...) {}
    return routes;
}