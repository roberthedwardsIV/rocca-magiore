#include "WaterwayRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace {
    // --- INLAND WATERWAY PHYSICS ---
    constexpr float STANDARD_BARGE_SPEED_KNOTS = 6.0f;  // Typical convoy speed
    constexpr float LOCK_CYCLE_TIME_MINUTES = 45.0f;    // Avg time to fill/empty + approach
    constexpr float DRAFT_BUFFER_METERS = 0.5f;         // Under-keel clearance required for safety
    
    // CEMT Class Capacities (Tonnage)
    // Class I (Spits): ~300t
    // Class IV (RHK): ~1500t
    // Class Vb (Jowi): ~4000t
    // Class VIb (Convoy): ~12000t
    constexpr float CAP_CLASS_I = 300.0f;
    constexpr float CAP_CLASS_IV = 1500.0f;
    constexpr float CAP_CLASS_V = 4000.0f;
    constexpr float CAP_CLASS_VI = 12000.0f;
}

// Constructor for initialization
WaterwayRoute::WaterwayRoute(long long id, std::string name)
    : BaseRoute(id, name, "waterway") {
    
    // Default Geometry (Standard Canal)
    max_draft_meters = 3.0f;       
    max_air_draft_meters = 7.0f;   
    channel_width_meters = 20.0f;
    cemt_class = 4;                // Class IV (Standard Europe)
    lock_count = 0;

    // Default Hydrology
    current_speed_knots = 0.0f;    // Canal (Still water)
    water_level_offset_m = 0.0f;   // Normal pool
    is_frozen = false;

    // Calculated Metrics
    effective_draft_meters = 2.5f;
    max_deadweight_tons = CAP_CLASS_IV;
    effective_sog_knots = STANDARD_BARGE_SPEED_KNOTS;
    lock_penalty_hours = 0.0f;
    travel_time_hours = 0.0f;
}


// Direct updates to static fields from OSM tags
void WaterwayRoute::parse_osm_tags() {
    // 1. CEMT CLASS (The primary capacity designator)
    if (tags.count("CEMT")) {
        std::string c = tags["CEMT"];
        // Roman numerals to int conversion (simplified)
        if (c.find("VI") != std::string::npos) cemt_class = 6;
        else if (c.find("V") != std::string::npos) cemt_class = 5;
        else if (c.find("IV") != std::string::npos) cemt_class = 4;
        else if (c.find("III") != std::string::npos) cemt_class = 3;
        else cemt_class = 1;
    }

    // 2. DIMENSIONS
    if (tags.count("maxdraft") || tags.count("depth")) {
        try { 
            std::string d = tags.count("maxdraft") ? tags["maxdraft"] : tags["depth"];
            max_draft_meters = std::stof(d); 
        } catch (...) { max_draft_meters = 3.0f; }
    }

    if (tags.count("maxheight") || tags.count("bridge:height")) {
        try { 
            std::string h = tags.count("maxheight") ? tags["maxheight"] : tags["bridge:height"];
            max_air_draft_meters = std::stof(h); 
        } catch (...) { max_air_draft_meters = 7.0f; }
    }

    // 3. INFRASTRUCTURE (Locks)
    // OSM often marks locks as nodes, but ways can have "lock=yes" or "lock_name"
    if (tags.count("lock") && tags["lock"] == "yes") {
        lock_count = 1; // Default to at least 1 if tagged
    }

    // 4. FLOW TYPE
    if (tags.count("waterway")) {
        if (tags["waterway"] == "river") {
            // Rivers usually have current
            current_speed_knots = 2.0f; // Default downstream assumption
        } else if (tags["waterway"] == "canal") {
            current_speed_knots = 0.0f; // Still water
        }
    }
    
    update_metrics();
}


// Updates to dynamic + calculated fields from Thalamus signaling
void WaterwayRoute::update_metrics() {
    // ---- 1. Draft & Capacity Calculation ----
    // Actual Depth = Chart Datum + Water Level Offset
    float actual_depth = max_draft_meters + water_level_offset_m;
    
    // Usable Draft = Actual - Safety Buffer
    effective_draft_meters = std::max(0.0f, actual_depth - DRAFT_BUFFER_METERS);
    
    // Light-Loading Logic:
    // If effective draft is less than design draft, capacity drops linearly.
    // Design draft approx: Class I (2m), Class IV (2.5m), Class V (3m)
    float design_draft = 2.5f; 
    if (effective_draft_meters < design_draft) {
        float load_factor = effective_draft_meters / design_draft;
        // Capacity penalty is severe (exponential) as fixed weight of barge eats buoyancy
        load_factor = std::pow(load_factor, 1.5f);
        
        // Base tonnage on CEMT class
        float base_cap = CAP_CLASS_IV;
        if (cemt_class >= 6) base_cap = CAP_CLASS_VI;
        else if (cemt_class == 5) base_cap = CAP_CLASS_V;
        else if (cemt_class <= 2) base_cap = CAP_CLASS_I;
        
        max_deadweight_tons = base_cap * load_factor;
    }

    // ---- 2. Speed Over Ground (Hydrology) ----
    // SOG = Vessel Speed + Current Vector
    // If current is negative (upstream), we subtract.
    effective_sog_knots = STANDARD_BARGE_SPEED_KNOTS + current_speed_knots;
    
    // If upstream current > vessel speed, we are stationary/sliding back
    if (effective_sog_knots < 0.5f) effective_sog_knots = 0.1f; // Crawling/Stalled

    if (is_frozen) {
        effective_sog_knots = 0.0f;
        max_deadweight_tons = 0.0f;
    }

    // ---- 3. Latency (Lock Penalties) ----
    // Time = (Distance / Speed) + (Locks * CycleTime)
    lock_penalty_hours = (lock_count * LOCK_CYCLE_TIME_MINUTES) / 60.0f;
    
    if (get_length_km() > 0 && effective_sog_knots > 0.1f) {
        // km to nm
        float dist_nm = get_length_km() * 0.539957f;
        float transit_time = dist_nm / effective_sog_knots;
        travel_time_hours = transit_time + lock_penalty_hours;
    } else {
        travel_time_hours = 9999.0f; // Blocked
    }
}


// Main signal routing function
void WaterwayRoute::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(route_mutex);
    std::string category = sig.value("category", "none");

    // Hydrology (Levels & Flow)
    if (category == "hydrology" || category == "water_level") {
        if (sig.contains("level_offset_m")) {
            water_level_offset_m = sig["level_offset_m"].get<float>();
        }
        if (sig.contains("current_speed_kts")) {
            current_speed_knots = sig["current_speed_kts"].get<float>();
        }
        if (sig.contains("ice")) {
            is_frozen = sig["ice"].get<bool>();
        }
    }

    // Infrastructure (Lock Status)
    else if (category == "lock" || category == "infrastructure") {
        // If locks are broken, penalty becomes infinite
        bool operational = sig.value("operational", true);
        if (!operational) {
            // Hack to represent closure via time penalty
            lock_penalty_hours = 999.0f; 
        }
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}


// JSON packager for archival
json WaterwayRoute::get_json_state() const {
    std::lock_guard<std::mutex> lock(route_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"cemt_class", cemt_class},
        {"max_tonnage", max_deadweight_tons},
        {"effective_draft", effective_draft_meters},
        {"sog_knots", effective_sog_knots},
        {"current_knots", current_speed_knots},
        {"lock_penalty_h", lock_penalty_hours},
        {"travel_time_h", travel_time_hours},
        {"last_update", last_update}
    };
}