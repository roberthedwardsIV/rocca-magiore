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

// --- MAIN ROUTER ---
void Dispatcher::route_signal(const json& sig) {
    std::string type = sig.value("entity_type", "unknown");

    // 1. MACRO ECONOMIC DATA
    if (type == "macro_economic") {
        double rf = sig["data"].value("risk_free_rate", 0.045);
        double spread = sig["data"].value("corporate_spread", 0.015);
        long long ts = sig.value("timestamp", 0LL);
        
        MacroData current = TickerRegistry::get_macro_data();
        // Update global risk-free rate for valuation models
        TickerRegistry::update_macro_data(rf, spread, current.equity_risk_premium, ts);

        // Notify all assets (Mines/Refineries) to re-calculate WACC
        GlobalRegistry::for_each_asset([&sig](std::shared_ptr<BaseAsset> asset) {
            asset->process_packet(sig);
        });
        std::cout << "[DISPATCHER] FRED Rates Updated." << std::endl;
    }

    // 2. DISASTER EVENTS
    else if (type == "earthquake" || type == "wildfire") {
        handle_event_signal(sig);
    }

    // 3. PHYSICAL ASSETS (Valuation Targets)
    else if (type == "mine" || type == "refinery" || type == "smelter") {
        handle_asset_signal(sig);
    }

    // 4. INFRASTRUCTURE: ROUTES (Linear Transport)
    else if (type == "rail_line" || type == "rail_mainline" || type == "rail_spur" ||
             type == "highway" || type == "highway_trunk" || type == "road" ||
             type == "pipeline" || type == "pipeline_line" ||
             type == "maritime_route" || 
             type == "air_route" || type == "air" ||
             type == "waterway" || type == "inland_waterway" ||
             type == "power_line" || type == "power_grid" || type == "conveyor") {
        handle_route_signal(sig);
    }

    // 5. INFRASTRUCTURE: HUBS (Nodal Points)
    else if (type == "port" || type == "maritime_port" || type == "maritime_dock" ||
             type == "airport" || type == "aerodrome" || type == "heliport" ||
             type == "rail_node" || type == "marshalling_yard" || type == "station" ||
             type == "warehouse" || type == "logistics_terminal" || type == "storage_tank" ||
             type == "substation" || type == "power_plant" || type == "water_reservoir") {
        handle_hub_signal(sig);
    }

    // 6. INFRASTRUCTURE: CHOKEPOINTS (Critical Bottlenecks)
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
        if (type != "unknown") {
            std::cerr << "[DISPATCHER] Warning: Unhandled entity type: " << type << std::endl;
        }
    }
}

// --- HANDLERS ---

void Dispatcher::handle_event_signal(const json& sig) {
    std::string id = sig.value("entity_id", "unknown");
    float lat = sig["data"].value("lat", 0.0f);
    float lon = sig["data"].value("lon", 0.0f);
    long long timestamp = sig.value("timestamp", 0LL);
    std::string type = sig.value("entity_type", "");

    // 1. Try to find existing event in memory
    std::shared_ptr<BaseEvent> target = GlobalRegistry::get_event(id);

    // 2. If not found, search by proximity (Is this an update to a nearby event?)
    if (!target) {
        std::string proximal_id = GlobalRegistry::find_event_by_proximity(lat, lon, timestamp, type);
        if (!proximal_id.empty()) {
            target = GlobalRegistry::get_event(proximal_id);
            id = proximal_id; // Merge into existing ID
        }
    }

    // 3. If still not found, check Database Archive (Resurrection)
    if (!target) {
        json archived = query_database_for_event(id, lat, lon, timestamp, type);
        if (!archived.is_null()) {
            if (type == "earthquake") target = std::make_shared<EarthquakeTracker>(archived);
            else if (type == "wildfire") target = std::make_shared<WildfireTracker>(archived);
            
            if (target) GlobalRegistry::register_event(id, target);
        }
    }

    // 4. Spawn New Event
    if (!target) {
        if (type == "earthquake") {
            std::cout << "[DISPATCHER] Spawning Earthquake: " << id << std::endl;
            target = std::make_shared<EarthquakeTracker>(sig);
            // Trigger social media recon to validate intensity
            trigger_twitter_recon(id, "earthquake", lat, lon, timestamp);
        } 
        else if (type == "wildfire") {
            std::cout << "[DISPATCHER] Spawning Wildfire: " << id << std::endl;
            target = std::make_shared<WildfireTracker>(sig);
            trigger_twitter_recon(id, "wildfire", lat, lon, timestamp);
        }
        if (target) GlobalRegistry::register_event(id, target);
    }
    
    // 5. Process & Propagate
    if (target) {
        target->process_packet(sig);
        // This calculates the physical blast radius and notifies affected infrastructure
        Propagator::propagate_event_impact(id); 
    }
}

void Dispatcher::handle_asset_signal(const json& sig) {
    int id = sig.value("asset_id", -1);
    if (id == -1) return;

    auto asset = GlobalRegistry::get_asset(id);
    if (asset) {
        asset->process_packet(sig["data"]);
        Propagator::propagate_asset_change(id); // Recalculate financial impact
    }
}

void Dispatcher::handle_route_signal(const json& sig) {
    long long id = sig.value("line_id", -1LL);
    std::string type = sig.value("entity_type", "");
    
    if (id == -1) return;

    auto route = GlobalRegistry::get_route(id, type);
    if (route) {
        // Ingest physical update (e.g., speed drop, closure)
        route->process_packet(sig["data"]); 
        
        // Calculate downstream impact on Hubs/Assets
        Propagator::propagate_route_change(id); 
    }
}

void Dispatcher::handle_hub_signal(const json& sig) {
    long long id = sig.value("hub_id", -1LL);
    // Fallback for messy data
    if (id == -1) id = sig.value("asset_id", -1LL); 
    
    std::string type = sig.value("entity_type", "");
    if (id == -1) return;

    auto hub = GlobalRegistry::get_hub(id, type);
    if (hub) {
        // Ingest physical update (e.g., congestion, power outage)
        hub->process_packet(sig["data"]);
        
        // Recalculate Hub's internal metrics (Capacity, Throughput)
        hub->update_status(); 
        
        // Calculate downstream impact on Routes leaving this Hub
        Propagator::propagate_hub_change(id);
    }
}

void Dispatcher::handle_chokepoint_signal(const json& sig) {
    int id = sig.value("cp_id", -1);
    // Fallback
    if (id == -1) id = sig.value("id", -1);

    std::string type = sig.value("entity_type", "");
    if (id == -1) return;

    auto cp = GlobalRegistry::get_chokepoint(id, type);
    if (cp) {
        // Ingest physical update (e.g., structural damage, maintenance)
        cp->process_packet(sig["data"]);
        
        // Calculate impact on the specific Route this ChokePoint controls
        Propagator::propagate_chokepoint_change(id); 
    }
}

void Dispatcher::handle_ticker_signal(const json& sig) {
    std::string symbol = sig.value("symbol", "");
    if (symbol.empty()) return;

    auto ticker = TickerRegistry::get_ticker(symbol);
    if (ticker) {
        ticker->process_quote(sig);
    }
}

// --- UTILITIES ---

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