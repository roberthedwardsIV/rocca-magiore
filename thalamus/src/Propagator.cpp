#include "Propagator.hpp"
#include "GlobalRegistry.hpp"
#include "DatabaseManager.hpp"
#include "finance/SignalEngine.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

// --- HELPER: SEISMIC PHYSICS ---
float estimate_pga(float mag, float dist_km) {
    float R = std::max(dist_km, 1.0f);
    float pga = (0.6f * std::exp(0.8f * mag)) / std::pow(R + 20.0f, 1.5f);
    return pga;
}

// --- MAIN EVENT ENTRY POINT ---
void Propagator::propagate_event_impact(const std::string& event_id) {
    auto event = GlobalRegistry::get_event(event_id);
    if (!event) return;

    float lat = event->get_lat();
    float lon = event->get_lon();
    std::string type = event->entity_type;

    if (type == "earthquake") {
        json state = event->to_json();
        float mag = state["final_mag"].get<float>();
        notify_infrastructure_of_seismic(event_id, lat, lon, mag);
    } 
    else if (type == "wildfire") {
        json state = event->to_json();
        float frp = state["current_frp"].get<float>();
        notify_infrastructure_of_wildfire(event_id, lat, lon, frp);
    }
}

// --- PHYSICS INJECTION: SEISMIC ---
void Propagator::notify_infrastructure_of_seismic(const std::string& event_id, float lat, float lon, float mag) {
    // 1. ChokePoints (Bridges/Dams)
    auto nearby_cps = check_chokepoint_exposure(lat, lon);
    for (const auto& hit : nearby_cps) {
        auto cp = GlobalRegistry::get_chokepoint(hit.cp_id, hit.type);
        if (!cp) continue;

        float local_pga = estimate_pga(mag, hit.dist_km);
        if (local_pga > 0.05f) {
            json sig;
            sig["category"] = "seismic";
            sig["event_id"] = event_id;
            sig["pga_g"] = local_pga;
            sig["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            
            cp->process_packet(sig);
            propagate_chokepoint_change(hit.cp_id);
        }
    }

    // 2. Assets (Mines/Refineries)
    auto nearby_assets = check_asset_exposure(lat, lon);
    for (const auto& hit : nearby_assets) {
        auto asset = GlobalRegistry::get_asset(hit.asset_id);
        if (!asset) continue;

        float local_pga = estimate_pga(mag, hit.dist_km);
        if (local_pga > 0.02f) {
            json sig;
            sig["category"] = "threat";
            sig["type"] = "earthquake";
            sig["severity"] = std::min(1.0f, local_pga * 2.0f);
            sig["pga_g"] = local_pga;
            
            asset->process_packet(sig);
            propagate_asset_change(hit.asset_id);
        }
    }
}

// --- PHYSICS INJECTION: WILDFIRE ---
void Propagator::notify_infrastructure_of_wildfire(const std::string& event_id, float lat, float lon, float frp) {
    // 1. Routes (Road/Rail/Power)
    auto nearby_routes = check_route_exposure(lat, lon);
    for (const auto& hit : nearby_routes) {
        auto route = GlobalRegistry::get_route(hit.line_id, hit.type);
        if (!route) continue;

        float impact = (frp / 500.0f) / std::max(1.0f, hit.dist_km); 
        if (impact > 0.1f) {
            json sig;
            sig["category"] = "environment";
            sig["type"] = "fire";
            sig["severity"] = std::min(1.0f, impact);
            if (hit.type == "power_grid") sig["hazard"] = "ionization";
            else sig["hazard"] = "visibility";

            route->process_packet(sig);
            propagate_route_change(hit.line_id);
        }
    }
}

// --- RIPPLE HANDLERS ---

void Propagator::propagate_chokepoint_change(int cp_id) {
    auto cp = GlobalRegistry::get_chokepoint(cp_id, ""); 
    if (!cp) return;

    float throughput_mod = cp->calculate_throughput_modifier();
    json state = cp->get_json_state();

    std::vector<long long> parent_routes = get_routes_for_chokepoint(cp_id);
    for (long long route_id : parent_routes) {
        auto route = GlobalRegistry::get_route(route_id);
        if (route) {
            json sig;
            sig["category"] = "infrastructure";
            sig["chokepoint_id"] = cp_id;
            sig["throughput_modifier"] = throughput_mod;
            sig["timestamp"] = state.value("last_update", 0LL);
            route->process_packet(sig);
            propagate_route_change(route_id);
        }
    }
}

void Propagator::propagate_route_change(long long route_id) {
    auto route = GlobalRegistry::get_route(route_id);
    if (!route) return;

    json r_state = route->get_json_state();
    float flow_capacity = 1.0f;
    float current_volume = 0.0f;

    if (r_state.contains("flow_tph")) { 
        current_volume = r_state["flow_tph"];
        flow_capacity = 1.0f; 
    } else if (r_state.contains("flow_vph")) { 
        current_volume = r_state["flow_vph"];
        flow_capacity = 1.0f; 
    } else if (r_state.contains("effective_speed_kts")) {
        float speed = r_state["effective_speed_kts"];
        flow_capacity = std::max(0.0f, speed / 15.0f);
    }

    // Downstream -> Hubs
    std::vector<long long> connected_hubs = get_hubs_for_route(route_id);
    for (long long hub_id : connected_hubs) {
        auto hub = GlobalRegistry::get_hub(hub_id);
        if (hub) {
            json sig;
            sig["category"] = "logistics";
            sig["route_id"] = route_id;
            sig["inbound_flow_mod"] = flow_capacity;
            hub->process_packet(sig);
            propagate_hub_change(hub_id);
        }
    }

    // Lateral -> ChokePoints
    if (current_volume > 0) {
        std::vector<int> route_cps = get_chokepoints_for_route(route_id);
        for (int cp_id : route_cps) {
            auto cp = GlobalRegistry::get_chokepoint(cp_id, "");
            if (cp) {
                json sig;
                sig["category"] = "traffic";
                sig["load_volume"] = current_volume;
                cp->process_packet(sig);
            }
        }
    }
}

void Propagator::propagate_hub_change(long long hub_id) {
    auto hub = GlobalRegistry::get_hub(hub_id);
    if (!hub) return;

    json h_state = hub->get_json_state();
    float congestion = h_state.value("congestion", 0.0f);
    float logistics_efficiency = 1.0f - congestion;

    std::vector<int> linked_assets = get_assets_for_hub(hub_id);
    for (int asset_id : linked_assets) {
        auto asset = GlobalRegistry::get_asset(asset_id);
        if (asset) {
            json sig;
            sig["category"] = "logistics";
            sig["hub_id"] = hub_id;
            sig["efficiency"] = logistics_efficiency;
            asset->process_packet(sig);
            propagate_asset_change(asset_id);
        }
    }

    if (congestion > 0.8f) {
        std::vector<long long> feeding_routes = get_routes_for_hub(hub_id);
        for (long long route_id : feeding_routes) {
            auto route = GlobalRegistry::get_route(route_id);
            if (route) {
                json sig;
                sig["category"] = "flow";
                sig["congestion_level"] = congestion;
                route->process_packet(sig);
            }
        }
    }
}

void Propagator::propagate_asset_change(int asset_id) {
    auto asset = GlobalRegistry::get_asset(asset_id);
    if (!asset) return;

    json state = asset->get_json_state();
    float op_health = state.value("op_health", 1.0f);
    float output_volume = 0.0f;
    
    if (state.contains("production_rate")) output_volume = state["production_rate"];
    else if (state.contains("throughput_tpd")) output_volume = state["throughput_tpd"];

    std::string entity_key = "ASSET_" + std::to_string(asset_id);
    SignalEngine::calculate_market_deltas(entity_key, op_health);

    std::vector<long long> export_routes = get_routes_for_asset(asset_id);
    for (long long route_id : export_routes) {
        auto route = GlobalRegistry::get_route(route_id);
        if (route) {
            json sig;
            sig["category"] = "traffic";
            sig["current_vph"] = output_volume; 
            route->process_packet(sig);
            propagate_route_change(route_id);
        }
    }
}