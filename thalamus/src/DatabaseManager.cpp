#include "DatabaseManager.hpp"
#include "db_conn.hpp"
#include <pqxx/pqxx>
#include <iostream>

static const std::string conn_str = hippocampus_conn();

bool init_database() {
    try {
        pqxx::connection C(conn_str);
        if (C.is_open()) {
            std::cout << "[DatabaseManager.cpp] Connected to Hippocampus." << std::endl;
            return true;
        }
    } catch (const std::exception &e) {
        std::cerr << "[DatabaseManager.cpp] (ERR) " << e.what() << std::endl;
    }
    return false;
}

std::vector<MatrixParam> get_matrix_parameters(int asset_id, const std::string& signal_category) {
    std::vector<MatrixParam> params;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        
        std::string sql = "SELECT ticker, beta_coefficient, lag_minutes, confidence_score, "
                          "EXTRACT(EPOCH FROM last_backtested) AS last_backtested_ts "
                          "FROM sensitivity_matrix "
                          "WHERE entity_id = 'ASSET_' || " + std::to_string(asset_id) + " "
                          "AND signal_category = " + N.quote(signal_category);
                          
        pqxx::result R = N.exec(sql);
        
        for (auto row : R) {
            params.push_back({
                row["ticker"].as<std::string>(),
                row["beta_coefficient"].as<double>(),
                row["lag_minutes"].as<int>(),
                row["confidence_score"].as<double>(),
                row["last_backtested_ts"].is_null() ? 0LL : static_cast<long long>(row["last_backtested_ts"].as<double>())
            });
        }
    } catch (const std::exception &e) {
        std::cerr << "[DB ERROR] Failed to fetch matrix params: " << e.what() << std::endl;
    }
    return params;
}

std::vector<int> get_downstream_assets(int asset_id) {
    std::vector<int> targets;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        
        std::string sql = "SELECT target_asset_id FROM asset_trade_links "
                          "WHERE origin_asset_id = " + std::to_string(asset_id) + " "
                          "AND active = TRUE";
                          
        pqxx::result R = N.exec(sql);
        for (auto row : R) {
            targets.push_back(row[0].as<int>());
        }
    } catch (const std::exception &e) {
        std::cerr << "[DatabaseManager.cpp] (ERR) Failed to fetch downstream targets: " << e.what() << std::endl;
    }
    return targets;
}

std::vector<AssetDistance> get_assets_near_location(double lat, double lon, double radius_km) {
    std::vector<AssetDistance> hit_assets;
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);
        
        std::string sql = 
            "SELECT id, "
            "ST_Distance(geom::geography, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + ", " + std::to_string(lat) + "), 4326)::geography) / 1000.0 AS dist_km "
            "FROM assets "
            "WHERE ST_DWithin(geom::geography, ST_SetSRID(ST_MakePoint(" + std::to_string(lon) + ", " + std::to_string(lat) + "), 4326)::geography, " + 
            std::to_string(radius_km * 1000.0) + ")";
                          
        pqxx::result R = N.exec(sql);
        for (auto row : R) {
            hit_assets.push_back({
                row["id"].as<int>(),
                row["dist_km"].as<double>()
            });
        }
    } catch (const std::exception &e) {
        std::cerr << "[DatabaseManager.cpp] (ERR) Spatial query failed: " << e.what() << std::endl;
    }
    return hit_assets;
}