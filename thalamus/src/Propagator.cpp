/**
* Propagator.cpp: 
*/
#include "Propagator.hpp"
#include "GlobalRegistry.hpp"
#include "DatabaseManager.hpp"
#include <iostream>

void Propagator::propagate_event_impact(const std::string& event_id) {
    auto event = GlobalRegistry::get_event(event_id);
    if (!event) return;

    float lat = event->get_lat();
    float lon = event->get_lon();

    notify_assets_of_seismic(event_id, lat, lon);
    notify_supply_of_seismic(event_id, lat, lon);
}

void Propagator::notify_assets_of_seismic(const std::string& event_id, float event_lat, float event_lon) {
    auto impacted_assets = check_asset_exposure(event_lat, event_lon);
    
    for (const auto& exposure : impacted_assets) {
        auto asset = GlobalRegistry::get_asset(exposure.asset_id);
        if (!asset) continue;

        json impact_pkt;
        impact_pkt["category"] = "threat";
        impact_pkt["severity"] = 1.0f / (1.0f + exposure.dist_km); 
        impact_pkt["reliability"] = 0.85f;
        impact_pkt["timestamp"] = 0LL; 

        asset->process_packet(impact_pkt);
        
        propagate_asset_change(exposure.asset_id);
    }
}

void Propagator::notify_supply_of_seismic(const std::string& event_id, float event_lat, float event_lon) {
    auto impacted_lines = check_supply_exposure(event_lat, event_lon);
    
    for (const auto& exposure : impacted_lines) {
        auto line = GlobalRegistry::get_supply_line(exposure.line_id, exposure.type);
        if (!line) continue;

        json impact_pkt;
        impact_pkt["category"] = "integrity";
        impact_pkt["severity"] = 1.0f / (1.0f + exposure.dist_km);
        impact_pkt["reliability"] = 0.90f;
        
        line->process_packet(impact_pkt);

        propagate_supply_change(exposure.line_id);
    }
}

void Propagator::propagate_asset_change(int asset_id) {
    auto asset = GlobalRegistry::get_asset(asset_id);
    if (!asset) return;

    json state = asset->get_json_state();
    float op_health = state.value("op_health", 1.0f);

    if (op_health < 0.6f) {
        std::vector<int> lines = get_connected_supply_lines(asset_id);
        for (int lid : lines) {
            auto line = GlobalRegistry::get_supply_line(lid);
            if (!line) continue;

            json flow_hit;
            flow_hit["category"] = "flow";
            flow_hit["severity"] = 1.0f - op_health;
            flow_hit["reliability"] = 0.9f;
            line->process_packet(flow_hit);
        }
    }
}

void Propagator::propagate_supply_change(int line_id) {
    auto line = GlobalRegistry::get_supply_line(line_id);
    if (!line) return;

    json state = line->get_json_state();
    float flow = state.value("flow_capacity", state.value("throughput", state.value("transit_capacity", 1.0f)));

    if (flow < 0.7f) {
        std::vector<int> assets = get_connected_assets(line_id);
        for (int aid : assets) {
            auto asset = GlobalRegistry::get_asset(aid);
            if (!asset) continue;

            json fin_hit;
            fin_hit["category"] = "fin";
            fin_hit["severity"] = 1.0f - flow;
            fin_hit["reliability"] = 0.8f;
            asset->process_packet(fin_hit);
        }
    }
}