#ifndef PROPAGATOR_HPP
#define PROPAGATOR_HPP

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

class Propagator {
public:
    // --- TOP LEVEL EVENT ENTRY ---
    static void propagate_event_impact(const std::string& event_id);

    // --- NODE RIPPLE HANDLERS (Recursive Graph Logic) ---
    
    // 1. ChokePoint (Constraint Node)
    // - Downstream: Restricts parent Route capacity.
    static void propagate_chokepoint_change(int cp_id);
    
    // 2. Route (Edge)
    // - Downstream: Updates Hub inbound flow.
    // - Lateral: Updates ChokePoint structural load (Traffic stress).
    // - Upstream: Notifies Asset of logistics availability.
    static void propagate_route_change(long long route_id);
    
    // 3. Hub (Storage/Transfer Node)
    // - Upstream: Backpressures Routes (Congestion).
    // - Upstream: Notifies Assets (Logistics Efficiency).
    static void propagate_hub_change(long long hub_id);
    
    // 4. Asset (Source Node)
    // - Downstream: Pushes Volume Demand to Routes.
    // - Market: Updates Financial Valuation.
    static void propagate_asset_change(int asset_id);

private:
    // --- PHYSICS INJECTION HELPERS ---
    static void notify_infrastructure_of_seismic(const std::string& event_id, float lat, float lon, float mag);
    static void notify_infrastructure_of_wildfire(const std::string& event_id, float lat, float lon, float frp);
};

#endif