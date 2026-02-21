/**
* Dispatcher.cpp: Central nervous system router.
* Directs raw signals to specific handlers based on entity_type.
* Ensures infrastructure signals reach Routes, Hubs, and Chokepoints.
*/
#include "Dispatcher.hpp"
#include "GlobalRegistry.hpp"
#include "DatabaseManager.hpp"
#include "events/EarthquakeTracker.hpp"
#include "events/WildfireTracker.hpp"
#include "tickers/TickerRegistry.hpp"
#include "Propagator.hpp"

#include <iostream>
#include <hiredis/hiredis.h>
#include <vector>
#include <algorithm>

// --- UTILITY HELPER TO PREVENT NULL CRASHES ---
std::string safe_string(const json& j, const std::string& key, const std::string& default_val = "") {
    if (j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return default_val;
}

// --- CONFIGURATION ---
const std::vector<std::string> PERSISTENT_TYPES = {
    "asset", "mine", "refinery", "smelter", "port", "power_plant", "factory",
    "supply_line", "rail_route", "rail_mainline", "rail_spur",
    "highway", "highway_trunk", "road", "pipeline", "pipeline_line",
    "maritime_route", "shipping_lane", "air_route", "air",
    "waterway", "inland_waterway", "power_line", "power_grid",
    "environment", "earthquake", "wildfire", "weather",
    "chokepoint", "hub", "bridge", "tunnel", "dam"
};

// --- MAIN ROUTER ---
void Dispatcher::route_signal(const json& sig) {
    std::string type = safe_string(sig, "entity_type", "unknown");

    // 0. PERSISTENCE LAYER (The "Memory")
    if (std::find(PERSISTENT_TYPES.begin(), PERSISTENT_TYPES.end(), type) != PERSISTENT_TYPES.end()) {
        save_to_database(sig);
    }

    // 1. MACRO ECONOMIC DATA
    if (type == "macro_economic") {
        double rf = sig["data"].value("risk_free_rate", 0.045);
        double spread = sig["data"].value("corporate_spread", 0.015);
        long long ts = sig.value("timestamp", 0LL);
        
        MacroData current = TickerRegistry::get_macro_data();
        TickerRegistry::update_macro_data(rf, spread, current.equity_risk_premium, ts);

        GlobalRegistry::for_each_asset([&sig](std::shared_ptr<BaseAsset> asset) {
            asset->process_packet(sig);
        });
        std::cout << "[DISPATCHER] FRED Rates Updated." << std::endl;
    }
    // 2. DISASTER EVENTS
    else if (type == "earthquake" || type == "wildfire" || type == "environment") {
        handle_event_signal(sig);
    }
    // 3. PHYSICAL ASSETS
    else if (type == "mine" || type == "refinery" || type == "smelter" || 
             type == "sand" || type == "aggregate" || type == "clay" || 
             type == "kaolin" || type == "phosphate") {
        handle_asset_signal(sig);
    }
    // 4. INFRASTRUCTURE: ROUTES
    else if (type == "supply_line" || 
             type == "rail_line" || type == "rail_mainline" || type == "rail_spur" ||
             type == "highway" || type == "highway_trunk" || type == "road" ||
             type == "pipeline" || type == "pipeline_line" ||
             type == "maritime_route" || type == "shipping_lane" ||
             type == "air_route" || type == "air" ||
             type == "waterway" || type == "inland_waterway" ||
             type == "power_line" || type == "power_grid" || type == "conveyor") {
        handle_route_signal(sig);
    }
    // 5. INFRASTRUCTURE: HUBS
    else if (type == "port" || type == "maritime_port" || type == "maritime_dock" ||
             type == "airport" || type == "aerodrome" || type == "heliport" ||
             type == "rail_node" || type == "marshalling_yard" || type == "station" ||
             type == "warehouse" || type == "logistics_terminal" || type == "storage_tank" ||
             type == "substation" || type == "power_plant" || type == "water_reservoir") {
        handle_hub_signal(sig);
    }
    // 6. INFRASTRUCTURE: CHOKEPOINTS
    else if (type == "bridge" || type == "tunnel" || 
             type == "border" || type == "border_crossing" ||
             type == "canal_lock" || type == "dam" || 
             type == "runway" || 
             type == "pumping_station" || type == "crane") {
        handle_chokepoint_signal(sig);
    }
    // 7. FINANCIAL INSTRUMENTS
    else if (type == "stock" || type == "future" || 
             type == "option" || type == "commodity_spot" || type == "ticker_update") {
        handle_ticker_signal(sig);
    }
    else {
        if (type != "Unknown" && type != "unknown" && type != "signal" && type != "keepalive" &&
            type != "sand" && type != "aggregate" && type != "clay" && type != "kaolin" && type != "phosphate") {
            std::cerr << "[DISPATCHER] Warning: Unhandled entity type: " << type << std::endl;
        }
    }
}

// --- HANDLERS ---

void Dispatcher::handle_event_signal(const json& sig) {
    std::string id = safe_string(sig, "entity_id", "unknown");
    if (id == "unknown") id = safe_string(sig, "id", "unknown");

    float lat = 0.0f; 
    float lon = 0.0f;
    
    if (sig.contains("data")) {
        lat = sig["data"].value("lat", 0.0f);
        lon = sig["data"].value("lon", 0.0f);
    } else {
        lat = sig.value("lat", 0.0f);
        lon = sig.value("lon", 0.0f);
    }

    long long timestamp = sig.value("timestamp", 0LL);
    std::string type = safe_string(sig, "entity_type", "");

    std::shared_ptr<BaseEvent> target = GlobalRegistry::get_event(id);

    if (!target) {
        std::string proximal_id = GlobalRegistry::find_event_by_proximity(lat, lon, timestamp, type);
        if (!proximal_id.empty()) {
            target = GlobalRegistry::get_event(proximal_id);
            id = proximal_id; 
        }
    }

    if (!target) {
        json archived = query_database_for_event(id, lat, lon, timestamp, type);
        if (!archived.is_null()) {
            if (type == "earthquake") target = std::make_shared<EarthquakeTracker>(archived);
            else if (type == "wildfire" || type == "environment") target = std::make_shared<WildfireTracker>(archived);
            
            if (target) GlobalRegistry::register_event(id, target);
        }
    }

    if (!target) {
        if (type == "earthquake") {
            std::cout << "[DISPATCHER] Spawning Earthquake: " << id << std::endl;
            target = std::make_shared<EarthquakeTracker>(sig);
            trigger_twitter_recon(id, "earthquake", lat, lon, timestamp);
        } 
        else if (type == "wildfire" || type == "environment") {
            std::cout << "[DISPATCHER] Spawning Wildfire/Env Event: " << id << std::endl;
            target = std::make_shared<WildfireTracker>(sig);
            trigger_twitter_recon(id, "wildfire", lat, lon, timestamp);
        }
        if (target) GlobalRegistry::register_event(id, target);
    }
    
    if (target) {
        target->process_packet(sig);
        Propagator::propagate_event_impact(id); 
    }
}

void Dispatcher::handle_asset_signal(const json& sig) {
    int id = sig.value("asset_id", -1);
    if (id == -1) return;

    auto asset = GlobalRegistry::get_asset(id);
    if (asset) {
        asset->process_packet(sig["data"]);
        Propagator::propagate_asset_change(id); 
    }
}

void Dispatcher::handle_route_signal(const json& sig) {
    long long id = sig.value("line_id", -1LL);
    std::string type = safe_string(sig, "entity_type", "");
    
    if (id == -1) return;

    auto route = GlobalRegistry::get_route(id, type);
    if (route) {
        route->process_packet(sig["data"]); 
        Propagator::propagate_route_change(id); 
    }
}

void Dispatcher::handle_hub_signal(const json& sig) {
    long long id = sig.value("hub_id", -1LL);
    if (id == -1) id = sig.value("asset_id", -1LL); 
    
    std::string type = safe_string(sig, "entity_type", "");
    if (id == -1) return;

    auto hub = GlobalRegistry::get_hub(id, type);
    if (hub) {
        hub->process_packet(sig["data"]);
        hub->update_status(); 
        Propagator::propagate_hub_change(id);
    }
}

void Dispatcher::handle_chokepoint_signal(const json& sig) {
    int id = sig.value("cp_id", -1);
    if (id == -1) id = sig.value("id", -1);

    std::string type = safe_string(sig, "entity_type", "");
    if (id == -1) return;

    auto cp = GlobalRegistry::get_chokepoint(id, type);
    if (cp) {
        cp->process_packet(sig["data"]);
        Propagator::propagate_chokepoint_change(id); 
    }
}

void Dispatcher::handle_ticker_signal(const json& sig) {
    std::string symbol = safe_string(sig, "symbol", "");
    if (symbol.empty()) return;

    auto ticker = TickerRegistry::get_ticker(symbol);
    if (ticker) {
        ticker->process_quote(sig);
    }
}

void Dispatcher::trigger_twitter_recon(const std::string& id, const std::string& type, float lat, float lon, long long ts) {
    redisContext* c = redisConnect("corpus_callosum", 6379);
    if (c && !c->err) {
        json task;
        task["task_id"] = id;       
        task["type"] = type;
        task["lat"] = lat;
        task["lon"] = lon;
        task["timestamp"] = ts;

        std::string payload = task.dump();
        redisCommand(c, "PUBLISH twitter_recon_tasks %s", payload.c_str());
        redisFree(c);
        std::cout << "[DISPATCHER] Recon dispatched: " << id << std::endl;
    } else {
        if(c) redisFree(c);
        std::cerr << "[DISPATCHER] Redis connection failed for Recon trigger." << std::endl;
    }
}