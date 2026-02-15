#include "PortHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- PORT GEOMETRY & PHYSICS CONSTANTS ---
    
    // Vessel Dimensions (for Berth Estimation)
    constexpr float BERTH_LEN_ULCV = 400.0f;       // Ultra Large Container Vessel
    constexpr float BERTH_LEN_CAPESIZE = 300.0f;   // Standard Ore Carrier
    constexpr float BERTH_LEN_PANAMAX = 230.0f;    // Standard Limit
    constexpr float BERTH_LEN_FEEDER = 150.0f;     // Coastal / Handymax

    // Cargo Densities (Tons per Cubic Meter / Effective Stack Density)
    constexpr float DENSITY_IRON_ORE = 2.5f;       // Heavy bulk
    constexpr float DENSITY_COAL = 0.85f;          // Voluminous bulk
    constexpr float DENSITY_STEEL_COIL = 3.5f;     // Dense breakbulk (with air gaps)
    constexpr float DENSITY_GEN_CARGO = 0.5f;      // Light freight

    // Stacking Physics (Safe Heights)
    constexpr float STACK_HEIGHT_BULK = 15.0f;     // Open piles (Ore/Coal)
    constexpr float STACK_HEIGHT_METALS = 4.0f;    // Safe limit for heavy coils/slabs
    constexpr float STACK_HEIGHT_TEU = 5.0f;       // Avg container stack height (tiers)

    // Mechanical Throughput Rates (Nameplate)
    constexpr float RATE_SHIPLOADER_ORE = 3000.0f; // TPH (High speed conveyor)
    constexpr float RATE_SHIPLOADER_GRAIN = 1000.0f;// TPH
    constexpr float RATE_CRANE_BREAKBULK = 150.0f; // TPH per gang/crane (Metals)
    constexpr float RATE_CRANE_CONTAINER = 25.0f;  // Moves per hour per crane
    constexpr float WEIGHT_PER_TEU = 12.0f;        // Avg tons per TEU

    // Operational Thresholds
    constexpr float CRITICAL_CONGESTION = 0.90f;   // The "Gridlock" point
    constexpr float DWELL_NORMAL_HOURS = 72.0f;    // 3 days
    constexpr float DWELL_CRISIS_HOURS = 240.0f;   // 10 days
}

// Constructor for initialization
PortHub::PortHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "port", lat, lon) {
    
    // Zero-init (Real values come from parse_osm_tags)
    number_of_berths = 0;
    max_draft_meters = 0.0f;
    total_quay_length_m = 0.0f;
    has_rail_connection = false;
    has_ro_ro_ramp = false;

    max_storage_teu = 0;
    current_storage_teu = 0;
    max_bulk_storage_tons = 0.0f;
    current_bulk_storage_tons = 0.0f;
    max_break_bulk_storage_m2 = 0.0f;
    current_break_bulk_tons = 0.0f;
    reefer_plugs = 0;

    gate_throughput_vph = 0.0f;
    loading_rate_tph = 0.0f;
    average_dwell_time_h = DWELL_NORMAL_HOURS;
    congestion_level = 0.0f;

    port_type = "general";
}

void PortHub::parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) {
    // 1. Port Categorization
    if (tags.count("harbour:category")) {
        std::string cat = tags.at("harbour:category");
        if (cat == "seaport") port_type = "container"; 
        else if (cat == "bulk" || cat == "industrial") port_type = "bulk";
        else if (cat == "general_cargo" || cat == "break_bulk") port_type = "break_bulk";
    }

    // 2. Physical Dimensions (Quay & Draft)
    if (tags.count("length")) {
        try { total_quay_length_m = std::stof(tags.at("length")); } catch (...) {}
    } else {
        // Fallback heuristic based on node type if length missing
        total_quay_length_m = (port_type == "container") ? 1000.0f : 400.0f;
    }

    if (tags.count("mooring:depth")) {
        try { max_draft_meters = std::stof(tags.at("mooring:depth")); } catch (...) {}
    } else {
        // Assume draft matches type requirements if unknown
        if (port_type == "container") max_draft_meters = 15.0f; // Panamax+
        else if (port_type == "bulk") max_draft_meters = 18.0f; // Capesize
        else max_draft_meters = 10.0f; // Handymax
    }

    // 3. Derive Berths from Geometry
    float berth_size = BERTH_LEN_FEEDER;
    if (max_draft_meters >= 18.0f) berth_size = BERTH_LEN_CAPESIZE;
    else if (max_draft_meters >= 14.0f) berth_size = BERTH_LEN_PANAMAX;
    
    number_of_berths = std::max(1, (int)(total_quay_length_m / berth_size));

    // 4. Rail Connectivity
    if (tags.count("railway")) has_rail_connection = true;

    // 5. Capacity Physics (Area -> Volume -> Mass)
    // We assume the port has a land area proportional to its quay length if not explicitly tagged.
    // Rule of Thumb: Yard depth ~500m for container/bulk terminals.
    float estimated_yard_area_m2 = total_quay_length_m * 500.0f; 
    if (tags.count("area")) { // If we have exact polygon area
        try { estimated_yard_area_m2 = std::stof(tags.at("area")); } catch(...) {}
    }

    if (port_type == "bulk") {
        // Volume = Area * Pile Height. Mass = Volume * Density (Ore)
        // Only ~60% of land is usable for piles (roads/conveyors take space)
        float usable_area = estimated_yard_area_m2 * 0.60f;
        max_bulk_storage_tons = usable_area * STACK_HEIGHT_BULK * DENSITY_IRON_ORE;
    } 
    else if (port_type == "break_bulk") {
        // Metals need warehouses or aprons. Lower stack height.
        // Efficiency lower (forklift lanes) -> 50% usable
        float usable_area = estimated_yard_area_m2 * 0.50f;
        max_break_bulk_storage_m2 = usable_area; // Used for capacity calc later
        // Implicit tonnage cap
        float cap_tons = usable_area * STACK_HEIGHT_METALS * DENSITY_STEEL_COIL;
        // Store metric for reference, though we track inventory against m2 usually
    }
    else {
        // Containers (TEU)
        // TEU footprint ~15m2. Stack height avg 3-5 tiers. 
        // 50% land usage efficiency (RTG lanes).
        float slots = (estimated_yard_area_m2 * 0.50f) / 15.0f;
        max_storage_teu = (int)(slots * STACK_HEIGHT_TEU);
    }
    
    update_metrics();
}

void PortHub::update_metrics() {
    // --- 1. Congestion Physics ---
    // Utilization = Current / Max
    if (port_type == "container" && max_storage_teu > 0) {
        congestion_level = (float)current_storage_teu / (float)max_storage_teu;
    } 
    else if (port_type == "bulk" && max_bulk_storage_tons > 0) {
        congestion_level = current_bulk_storage_tons / max_bulk_storage_tons;
    } 
    else if (port_type == "break_bulk" && max_break_bulk_storage_m2 > 0) {
        // Estimate density of current inventory
        // Assume Break Bulk is mostly Steel/Copper (Heavy)
        float current_volume = current_break_bulk_tons / DENSITY_STEEL_COIL;
        float current_area_used = current_volume / STACK_HEIGHT_METALS;
        congestion_level = current_area_used / max_break_bulk_storage_m2;
    } 
    else {
        congestion_level = 0.0f;
    }

    // --- 2. Loading Rate Physics (Equipment * Efficiency) ---
    // Efficiency drops as yard gets congested (harder to dig out cargo)
    float yard_efficiency = 1.0f;
    if (congestion_level > CRITICAL_CONGESTION) yard_efficiency = 0.5f;

    if (port_type == "bulk") {
        // Ship Loaders (Continuous)
        // Assume 1 loader per berth
        loading_rate_tph = (float)number_of_berths * RATE_SHIPLOADER_ORE * yard_efficiency;
    } 
    else if (port_type == "break_bulk") {
        // Cranes (Discrete Lifts)
        // Assume 2 cranes per berth
        float cranes = (float)number_of_berths * 2.0f;
        loading_rate_tph = (cranes * RATE_CRANE_BREAKBULK) * yard_efficiency;
    } 
    else {
        // Containers (Discrete Moves)
        // 3 Gantry Cranes per berth (standard for deep sea)
        float cranes = (float)number_of_berths * 3.0f;
        float moves_per_hour = cranes * RATE_CRANE_CONTAINER;
        // Convert to Mass for standardized output (approximate)
        loading_rate_tph = (moves_per_hour * WEIGHT_PER_TEU) * yard_efficiency;
    }

    // --- 3. Dwell Time Physics (Exponential Decay) ---
    // If yard is full, dwell time explodes because new cargo sits in buffer or on trucks.
    if (congestion_level > 0.95f) {
        average_dwell_time_h = DWELL_CRISIS_HOURS; // System lockup
    } else if (congestion_level > 0.80f) {
        // Exponential ramp between Normal and Crisis
        float severity = (congestion_level - 0.80f) / 0.15f; // 0.0 to 1.0
        average_dwell_time_h = DWELL_NORMAL_HOURS + (severity * (DWELL_CRISIS_HOURS - DWELL_NORMAL_HOURS));
    } else {
        average_dwell_time_h = DWELL_NORMAL_HOURS;
    }
}

void PortHub::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(hub_mutex);
    std::string category = sig.value("category", "none");

    // Inventory Updates
    if (category == "logistics" || category == "inventory") {
        if (sig.contains("teu_count")) current_storage_teu = sig["teu_count"].get<int>();
        if (sig.contains("bulk_tons")) current_bulk_storage_tons = sig["bulk_tons"].get<float>();
        if (sig.contains("metal_tons")) current_break_bulk_tons = sig["metal_tons"].get<float>();
    }
    
    // Operational Degradation (Equipment Failure / Strikes)
    else if (category == "op" || category == "labor") {
        if (sig.contains("efficiency")) {
            // Efficiency signal (0.0 to 1.0) directly scales loading rate
            float eff = sig["efficiency"].get<float>();
            // Apply transient penalty to the base rate calculation
            loading_rate_tph *= eff; 
        }
        if (sig.contains("strike") && sig["strike"].get<bool>()) {
            loading_rate_tph = 0.0f; // Port paralysis
        }
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

std::string PortHub::get_max_vessel_class() const {
    // Classification based on Draft (Physical Limit)
    if (max_draft_meters >= 20.0f) return "Valemax / Chinamax";
    if (max_draft_meters >= 18.0f) return "Capesize";
    if (max_draft_meters >= 15.0f) return "New Panamax";
    if (max_draft_meters >= 12.0f) return "Panamax";
    if (max_draft_meters >= 10.0f) return "Handymax";
    return "Feeder";
}

json PortHub::get_json_state() const {
    std::lock_guard<std::mutex> lock(hub_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"port_type", port_type},
        {"congestion", congestion_level},
        {"draft_m", max_draft_meters},
        {"berths", number_of_berths},
        {"quay_len_m", total_quay_length_m},
        {"loading_tph", loading_rate_tph},
        {"storage", {
            {"teu", current_storage_teu},
            {"bulk_tons", current_bulk_storage_tons},
            {"metal_tons", current_break_bulk_tons},
            {"max_bulk_tons", max_bulk_storage_tons},
            {"max_break_m2", max_break_bulk_storage_m2}
        }},
        {"max_vessel", get_max_vessel_class()},
        {"last_update", last_update}
    };
}