#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include <libpq-fe.h>
#include <chrono>

using json = nlohmann::json;

//CURRENTLY JUST SAVES SIGNALS TO DATABASE, WILL NEED TO ADD REDIS/SOCKET POST FOR FURTHER SIGNAL INTEGRATION BY EXECUTION/RISK ENGINES

struct Asset { 
    int id; 
    std::string name, ticker; 
    double lat, lon, radius; 
};

struct MilZone { 
    std::string name, type; 
    double lat, lon, radius; 
};

struct Profile { 
    std::string owner, category; 
    bool is_watchlist; 
};

std::vector<Asset> global_assets;
std::vector<MilZone> global_mil_zones;
std::map<std::string, Profile> aircraft_registry; 
std::map<std::string, json> live_sky; // Keyed by icao_hex

// Helper haversine function to convert coordinates to distances
double get_dist(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = pow(sin(dLat / 2), 2) + pow(sin(dLon / 2), 2) * cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0);
    return 6371.0 * (2 * asin(sqrt(a)));
}

// Helper fuction that connects to database
PGconn* connect_db() {
    return PQconnectdb("dbname=rocco_commodities user=rocco_admin password=REMOVED host=rocco_db port=5432");
}

// Helper context function that loads all assets, military zones and aircraft on our watchlist from our database
void load_context() {
    PGconn* conn = connect_db();
    if (PQstatus(conn) != CONNECTION_OK) {
        std::cerr << "[CRITICAL] DB Connection failed: " << PQerrorMessage(conn) << std::endl;
        return;
    }
    PGresult* res = PQexec(conn, "SELECT a.id, a.name, a.latitude, a.longitude, a.sensitivity_radius_km, o.ticker FROM assets a LEFT JOIN asset_ownership o ON a.id = o.asset_id;");
    for(int i=0; i < PQntuples(res); i++) {
        global_assets.push_back({
            std::stoi(PQgetvalue(res,i,0)), PQgetvalue(res,i,1), PQgetvalue(res,i,5),
            std::stod(PQgetvalue(res,i,2)), std::stod(PQgetvalue(res,i,3)), std::stod(PQgetvalue(res,i,4))
        });
    }
    res = PQexec(conn, "SELECT name, zone_type, latitude, longitude, radius_km FROM military_zones;");
    for(int i=0; i < PQntuples(res); i++) {
        global_mil_zones.push_back({PQgetvalue(res,i,0), PQgetvalue(res,i,1), std::stod(PQgetvalue(res,i,2)), std::stod(PQgetvalue(res,i,3)), std::stod(PQgetvalue(res,i,4))});
    }
    res = PQexec(conn, "SELECT icao_hex, owner_entity, category, is_watchlist FROM aircraft_profiles;");
    for(int i=0; i < PQntuples(res); i++) {
        aircraft_registry[PQgetvalue(res,i,0)] = {PQgetvalue(res,i,1), PQgetvalue(res,i,2), std::string(PQgetvalue(res,i,3)) == "t"};
    }

    std::cout << "[NEXUS] Context Loaded: " << global_assets.size() << " assets, " << aircraft_registry.size() << " profiles.\n";
    PQclear(res);
    PQfinish(conn);
}

// Function that handles seismic monitor signals (called when signal source is seismic)
void handle_seismic(const json& sig) {
    double s_lat = sig["coords"][0];
    double s_lon = sig["coords"][1];
    std::string sta = sig["station_id"];
    PGconn* conn = connect_db();

    for (auto& det : sig["detections"]) {
        std::string cls = det["class"];
        double conf = det["conf"];
        bool is_voided = false;
        // Earthquake Passover Filter (Void if plane within 5km/15k ft)
        if (cls == "Earthquake") {
            for (auto const& [icao, flight] : live_sky) {
                if (get_dist(s_lat, s_lon, flight["coords"][0], flight["coords"][1]) < 5.0 && (double)flight["metrics"]["alt"] < 15000) {
                    is_voided = true; break;
                }
            }
        }
        // Explosion/Military Filter (Void if in Base or Conflict Zone)
        if (cls == "Military/Explosion") {
            for (auto& zone : global_mil_zones) {
                if (get_dist(s_lat, s_lon, zone.lat, zone.lon) < zone.radius) {
                    is_voided = true; break;
                }
            }
        }
        // Asset Mapping (Searches all assets to see if earthquake occured within its radius using get_dist())
        int asset_id = -1;
        for (auto& asset : global_assets) {
            if (get_dist(s_lat, s_lon, asset.lat, asset.lon) <= asset.radius) {
                asset_id = asset.id; break;
            }
        }
        // CONTINUOUS LOGGING: Temporary in case we end up using for backtesting/review of systems
        std::string q = "INSERT INTO signal_logs (station_id, classification, confidence, impacted_asset_id, is_voided) VALUES ('" +
                        sta + "', '" + cls + "', " + std::to_string(conf) + ", " +
                        (asset_id == -1 ? "NULL" : std::to_string(asset_id)) + ", " + (is_voided ? "TRUE" : "FALSE") + ");";
        PQexec(conn, q.c_str());
        
        if (!is_voided && asset_id != -1) {
             std::cout << "[PULSE] Valid " << cls << " detected at Asset ID " << asset_id << "\n";
        }
    }
    PQfinish(conn);
}

// Function that handles aviation radar monitor signals (called when singal source is aviation)
void handle_aviation(const json& sig) {
    std::string icao = sig["icao_hex"];
    live_sky[icao] = sig;

    double a_lat = sig["coords"][0];
    double a_lon = sig["coords"][1];
    PGconn* conn = connect_db();

    for (auto& asset : global_assets) {
        if (get_dist(a_lat, a_lon, asset.lat, asset.lon) < asset.radius) {
            
            // Log signal if plane from our watchlist pops up on radar
            if (aircraft_registry.count(icao)) {
                auto prof = aircraft_registry[icao];
                std::cout << "[INTEL] " << prof.category << " arrival: " << prof.owner << " at " << asset.name << "\n";
                std::string q = "INSERT INTO signal_logs (station_id, classification, impacted_asset_id, flight_icao, flight_category) VALUES ('AVIATION', 'Watchlist_Arrival', " + 
                                std::to_string(asset.id) + ", '" + icao + "', '" + prof.category + "');";
                PQexec(conn, q.c_str());
            } 
            // Logging all PJs near all assets to eventually figure out who they are with a separate model
            else if (sig["metrics"]["owner"] == "Unknown") {
                std::string q = "INSERT INTO aircraft_discovery_logs (icao_hex, station_id, lat, lon, altitude_ft) VALUES ('" +
                                icao + "', 'AERIAL', " + std::to_string(a_lat) + ", " + std::to_string(a_lon) + ", " + std::to_string((double)sig["metrics"]["alt"]) + ");";
                PQexec(conn, q.c_str());
                std::cout << "[DISCOVERY] New Private Jet detected near " << asset.name << ". Logged for NN training.\n";
            }
        }
    }
    PQfinish(conn);
}

// ----------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------
int main() {
    load_context();
    redisContext *c = redisConnect("redis", 6379);
    
    redisReply *reply = (redisReply*)redisCommand(c, "SUBSCRIBE raw_signals");
    freeReplyObject(reply);

    std::cout << "[NEXUS] Operational. Monitoring Global Nexus Bus.\n";

    while(redisGetReply(c, (void**)&reply) == REDIS_OK) {
    if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
        try {
            std::string payload = reply->element[2]->str;
            if (payload.empty() || payload[0] == '<') {
                std::cerr << "[WARN] Received non-JSON payload (likely HTML error): " << payload.substr(0, 50) << "...\n";
            } else {
                json sig = json::parse(payload);
                if (sig["source"] == "seismic") handle_seismic(sig);
                else if (sig["source"] == "aviation") handle_aviation(sig);
            }
        } catch (const json::parse_error& e) {
            std::cerr << "[JSON ERR] Parse failed: " << e.what() << "\n";
            std::cerr << "Offending payload: " << reply->element[2]->str << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[NEXUS ERR] General error: " << e.what() << "\n";
        }
    }
    freeReplyObject(reply);
    }
    return 0;
}