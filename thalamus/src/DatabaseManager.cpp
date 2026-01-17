#include "DatabaseManager.hpp"
#include <pqxx/pqxx>
#include <iostream>

const std::string conn_str = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";

bool init_database() {
    try {
        pqxx::connection C(conn_str);
        if (C.is_open()) {
            std::cout << "[DB] Successfully connected to Hippocampus: " << C.dbname() << std::endl;
            
            // Optional: Run a quick check for the PostGIS extension
            pqxx::work W(C);
            W.exec("SELECT postgis_full_version();");
            W.commit();
            
            return true;
        }
    } catch (const std::exception &e) {
        std::cerr << "[DB ERROR] Initialization failed: " << e.what() << std::endl;
    }
    return false;
}

void save_to_database(const json& final_state) {
    try {
        pqxx::connection C(conn_str);
        pqxx::work W(C);

        std::string id = final_state.value("entity_id", "unknown");
        float mag = final_state["data"].value("mag", 0.0f); 
        float intensity = final_state["data"].value("mmi", 0.0f);
        float lat = final_state["data"].value("lat", 0.0f);
        float lon = final_state["data"].value("lon", 0.0f);
        long long ts = final_state.value("timestamp", 0LL);

        std::string sql = "INSERT INTO earthquakes (id, magnitude, intensity, lat, lon, start_time) "
                          "VALUES (" + W.quote(id) + ", " + 
                          std::to_string(mag) + ", " + 
                          std::to_string(intensity) + ", " + 
                          std::to_string(lat) + ", " + 
                          std::to_string(lon) + ", " + 
                          std::to_string(ts) + ") "
                          "ON CONFLICT (id) DO UPDATE SET magnitude = EXCLUDED.magnitude;";

        W.exec(sql);
        W.commit();
    } catch (const std::exception &e) {
        std::cerr << "[DB ERROR] Save failed: " << e.what() << std::endl;
    }
}

json query_database_for_event(std::string id, float lat, float lon, long long timestamp) {
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);

        // Spatial query: Search by ID OR by physical proximity (150km and 60s)
        std::string sql = "SELECT * FROM earthquakes WHERE id = " + N.quote(id) + 
                          " OR (ST_Distance(ST_MakePoint(lon, lat), ST_MakePoint(" + 
                          std::to_string(lon) + ", " + std::to_string(lat) + ")) < 150000 " +
                          " AND ABS(start_time - " + std::to_string(timestamp) + ") < 60000) LIMIT 1;";

        pqxx::result R = N.exec(sql);

        if (!R.empty()) {
            // Convert the DB row back into a JSON signal
            json j;
            j["entity_id"] = R[0]["id"].as<std::string>();
            j["data"]["mag"] = R[0]["magnitude"].as<float>();
            j["data"]["mmi"] = R[0]["intensity"].as<float>();
            j["data"]["lat"] = R[0]["lat"].as<float>();
            j["data"]["lon"] = R[0]["lon"].as<float>();
            j["timestamp"] = R[0]["start_time"].as<long long>();
            return j;
        }
    } catch (const std::exception &e) {
        std::cerr << "[DB ERROR] Query failed: " << e.what() << std::endl;
    }
    return json();
}