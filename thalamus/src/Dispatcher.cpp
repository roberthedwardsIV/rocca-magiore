/**
* Dispatcher.cpp: routes each raw_signal to its proper handling functions based on its
*                 entity_type. Each type of signal is then checked against list of active
*                 state vectors to determine if merging or spawning is needed. Dispatcher 
*                 gives the proper instructions to the GlobalRegistry + TickerRegistry 
*                 and sends signals for propagation/processing. 
*/
#include "Dispatcher.hpp"
#include "GlobalRegistry.hpp"
#include "DatabaseManager.hpp"
#include "events/EarthquakeTracker.hpp"
#include "tickers/TickerRegistry.hpp"
#include "Propagator.hpp"

#include <iostream>
#include <hiredis/hiredis.h>


// Signal router (by "entity_type")
void Dispatcher::route_signal(const json& sig) {
    std::string type = sig.value("entity_type", "unknown");
    
    // FRED MARKET DATA UPDATES
    if (type == "macro_economic") {
        double rf = sig["data"].value("risk_free_rate", 0.045);
        double spread = sig["data"].value("corporate_spread", 0.015);
        long long ts = sig.value("timestamp", 0LL);
        
        MacroData current = TickerRegistry::get_macro_data();
        double erp = current.equity_risk_premium;

        TickerRegistry::update_macro_data(rf, spread, erp, ts);

        GlobalRegistry::for_each_asset([&sig](std::shared_ptr<BaseAsset> asset) {
            asset->process_packet(sig);
        });
        std::cout << "[DISPATCHER] FRED Rates Updated in TickerRegistry." << std::endl;
    }

    // EVENTS HANDLING
    else if (type == "earthquake") {
        handle_event_signal(sig);

    // ASSET HANDLING
    } else if (type == "mine" || type == "refinery") {
        handle_asset_signal(sig);

    // SUPPLY LINE HANDLING
    } else if (type == "rail_line" || type == "rail_yard" || 
               type == "maritime_route" || type == "maritime_port" ||
               type == "pipeline_line" || type == "pipeline_station" ||
               type == "highway" || type == "airport" || 
               type == "airspace" || type == "canal_route" || 
               type == "canal_lock") {
        handle_supply_signal(sig);
    
    // TICKER HANDLING
    } else if (type == "stock" || type == "future" || 
             type == "option" || type == "commodity_spot") {
        handle_ticker_signal(sig);

    } else {
        std::cerr << "[DISPATCHER] Unknown entity type: " << type << std::endl;
    }
}


// ALL 3 HANDLER FUNCTIONS BELOW PASS SIGNAL OFF TO PROPAGATOR
// Handles event signals by searching for existing events to merge with or spawning a new one + applying signal data
void Dispatcher::handle_event_signal(const json& sig) {
    std::string id = sig.value("entity_id", "unknown");
    float lat = sig["data"].value("lat", 0.0f);
    float lon = sig["data"].value("lon", 0.0f);
    long long timestamp = sig.value("timestamp", 0LL);
    

    std::shared_ptr<BaseEvent> target = nullptr;

    // EXISTING REGISTRY SEARCH (ID MATCHES)
    target = GlobalRegistry::get_event(id);

    // PROXIMITY REGISTRY SEARCH (TIME+DIST MATCHES)
    if (!target) {
        std::string proximal_id = GlobalRegistry::find_event_by_proximity(lat, lon, timestamp, "earthquake");
        if (!proximal_id.empty()) {
            target = GlobalRegistry::get_event(proximal_id);
            id = proximal_id; 
        }
    }

    // DATABASE ARCHIVE SEARCH (TIME+DIST MATCHES)
    if (!target) {
        json archived = query_database_for_event(id, lat, lon, timestamp, "earthquake");
        if (!archived.is_null()) {
            target = std::make_shared<EarthquakeTracker>(archived);
            GlobalRegistry::register_event(id, target);
        }
    }

    // EARTHQUAKE EVENT SPAWN
    if (!target && sig.value("entity_type", "") == "earthquake") {
        std::cout << "[DISPATCHER] Spawning new EarthquakeTracker: " << id << std::endl;
        target = std::make_shared<EarthquakeTracker>(sig);
        GlobalRegistry::register_event(id, target);

        //CALLS TWITTER RECON TO VERIFY EARTHQUAKE SIGNAL
        trigger_twitter_recon(id, "earthquake", lat, lon, timestamp);

    } // else if {other event signal types to be added here once built}

    if (target) {
        target->process_packet(sig);
        Propagator::propagate_event_impact(id);
    }
}


// Handles asset signals by finding the applicable asset and applying signal data
void Dispatcher::handle_asset_signal(const json& sig) {
    int id = sig.value("asset_id", -1);
    if (id == -1) return;
    auto asset = GlobalRegistry::get_asset(id);
    if (asset) {
        asset->process_packet(sig["data"]);
        Propagator::propagate_asset_change(id);
    }
}


// Handles supply line signals by finding the applicable supply line and applying signal data
void Dispatcher::handle_supply_signal(const json& sig) {
    int id = sig.value("line_id", -1);
    std::string type = sig.value("entity_type", "");
    if (id == -1 || type.empty()) return;

    auto supply = GlobalRegistry::get_supply_line(id, type);
    if (supply) {
        supply->process_packet(sig["data"]);
        Propagator::propagate_supply_change(id); 
    }
}


// Handles ticker signals by updating ticker registry and applying new data packets
void Dispatcher::handle_ticker_signal(const json& sig) {
    std::string symbol = sig.value("symbol", "");
    if (symbol.empty()) return;

    auto ticker = TickerRegistry::get_ticker(symbol);
    if (ticker) {
        ticker->process_quote(sig);
    }
}


// Helper function: triggers a Twitter search by posting instructions to the "twitter_recon_tasks" redis channel
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
        std::cout << "[DISPATCHER] Triggered " << type << " recon for " << id << std::endl;
    }
}


// Helper function: extracts the earthquake station codes from the STA_NET codes in the db
std::string Dispatcher::extract_station_prefix(const std::string& entity_id) {
    size_t last_underscore = entity_id.find_last_of('_');
    if (last_underscore != std::string::npos) {
        return entity_id.substr(0, last_underscore);
    }
    return entity_id;
}