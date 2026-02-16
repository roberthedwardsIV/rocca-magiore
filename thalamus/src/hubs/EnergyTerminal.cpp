#include "EnergyTerminal.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- ENERGY PHYSICS CONSTANTS ---
    
    // Densities
    constexpr float DENSITY_LNG = 450.0f;       
    constexpr float DENSITY_CRUDE = 850.0f;     
    
    // Conversion Factors
    constexpr float LNG_M3_TO_MMBTU = 23.0f;    // Approx
    constexpr float MTPA_TO_M3H_LNG = 254.0f;   // 1 MTPA approx 254 m3/hr continuous
    
    // Operational Baselines
    constexpr float BOIL_OFF_MODERN = 0.05f;    // % per day (Modern insulation)
    constexpr float BOIL_OFF_OLDER = 0.15f;     // % per day
    constexpr float PUMP_RATE_TANKER = 10000.0f;// m3/hr (High speed loading)
}

EnergyTerminal::EnergyTerminal(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "energy_terminal", lat, lon) {
    
    // Defaults
    commodity_type = "crude_oil";
    terminal_role = "storage_hub";
    
    num_tanks = 0;
    max_storage_m3 = 0.0f;
    max_flow_rate_m3h = 5000.0f; // Generic pump station
    
    liquefaction_capacity_mtpa = 0.0f;
    regas_capacity_mmscfd = 0.0f;
    boil_off_rate_pct_day = 0.0f;

    current_inventory_m3 = 0.0f;
    current_flow_rate = 0.0f;
    current_pressure_psi = 0.0f;
    active_berths = 0.0f;

    utilization_rate = 0.0f;
    efficiency_factor = 1.0f;
    is_maintenance = false;
}

void EnergyTerminal::parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) {
    // 1. Commodity Identification
    if (tags.count("substance")) commodity_type = tags.at("substance");
    else if (tags.count("product")) commodity_type = tags.at("product");
    
    if (commodity_type == "lng" || commodity_type == "natural_gas") {
        commodity_type = "lng";
        boil_off_rate_pct_day = BOIL_OFF_MODERN;
    } else {
        // Default to oil if unsure
        if (commodity_type.empty()) commodity_type = "crude_oil";
        boil_off_rate_pct_day = 0.001f; // Negligible for oil
    }

    // 2. Role Inference
    if (commodity_type == "lng") {
        // Heuristic: default to Import as it's more common in consumption zones unless tagged otherwise
        terminal_role = "import_regasification"; 
    }

    // 3. Storage Geometry
    if (tags.count("tank_count")) {
        try { num_tanks = std::stoi(tags.at("tank_count")); } catch(...) { num_tanks = 1; }
    } else {
        num_tanks = 1; 
    }

    // Volume estimation
    float tank_volume = 50000.0f; // Default large industrial tank (m3)
    if (commodity_type == "lng") tank_volume = 160000.0f; // Standard full containment tank
    
    max_storage_m3 = (float)num_tanks * tank_volume;

    // 4. Flow Limits
    if (commodity_type == "lng") {
        // Heuristic: 1 Tank ~ 3 MTPA capacity support usually
        liquefaction_capacity_mtpa = (float)num_tanks * 3.0f; 
        max_flow_rate_m3h = liquefaction_capacity_mtpa * MTPA_TO_M3H_LNG;
    } else {
        // Oil pumping
        max_flow_rate_m3h = 5000.0f * (float)num_tanks; 
    }
}

void EnergyTerminal::update_metrics() {
    // 1. Boil-off Loss (LNG Only)
    // Inventory evaporates daily.
    if (commodity_type == "lng" && current_inventory_m3 > 0) {
        float daily_loss = current_inventory_m3 * (boil_off_rate_pct_day / 100.0f);
        // We calculate hourly loss for the tick
        float hourly_loss = daily_loss / 24.0f;
        current_inventory_m3 -= hourly_loss; 
        if (current_inventory_m3 < 0) current_inventory_m3 = 0;
    }

    // 2. Flow Constraints
    float effective_capacity = max_flow_rate_m3h * efficiency_factor;
    
    if (is_maintenance) effective_capacity *= 0.2f; // Severe restriction

    if (current_flow_rate > effective_capacity) {
        current_flow_rate = effective_capacity; // Choked
    }

    // 3. Utilization
    if (max_storage_m3 > 0) {
        utilization_rate = current_inventory_m3 / max_storage_m3;
    }
}

void EnergyTerminal::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(hub_mutex);
    std::string category = sig.value("category", "none");

    // Inventory Signals
    if (category == "inventory" || category == "storage") {
        if (sig.contains("volume_m3")) current_inventory_m3 = sig["volume_m3"].get<float>();
        if (sig.contains("fill_pct")) current_inventory_m3 = max_storage_m3 * sig["fill_pct"].get<float>();
    }
    
    // Flow/Operational Signals
    else if (category == "flow" || category == "scada") {
        if (sig.contains("rate")) current_flow_rate = sig["rate"].get<float>();
        if (sig.contains("pressure")) current_pressure_psi = sig["pressure"].get<float>();
    }

    // Maintenance
    else if (category == "maintenance") {
        is_maintenance = sig.value("active", false);
        if (!is_maintenance) efficiency_factor = 1.0f;
        else efficiency_factor = 0.5f; // Degraded state
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

json EnergyTerminal::get_json_state() const {
    std::lock_guard<std::mutex> lock(hub_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"commodity", commodity_type},
        {"role", terminal_role},
        {"storage_m3", current_inventory_m3},
        {"capacity_m3", max_storage_m3},
        {"flow_rate", current_flow_rate},
        {"max_flow", max_flow_rate_m3h},
        {"utilization", utilization_rate},
        {"boil_off", boil_off_rate_pct_day},
        {"last_update", last_update}
    };
}