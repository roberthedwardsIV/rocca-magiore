#include "WaterwayRoute.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

WaterwayRoute::WaterwayRoute(long long id, std::string name)
    : BaseRoute(id, name, "waterway") {
    
    // Default Physical Geometry (Small River / Feeder Canal)
    max_draft_meters = 2.5f;   // Standard barge draft
    air_draft_meters = 7.0f;   // Bridge clearance (2 container stack)
    beam_meters = 9.5f;        // Lock width (Freyssinet gauge)
    cemt_class = "IV";         // Standard European waterway

    // Default Hydrology (Normal Flow)
    current_speed_kmh = 3.0f;  // Gentle current
    water_level_stage = 0.0f;  // Normal pool
    is_frozen = false;
    is_canal = false;

    // Default Navigability
    navigability_status = 0;   // Open
    threat_level = 0;          // Safe
    hazard_type = "none";

    // Operational defaults
    upstream_speed_kmh = 8.0f;
    downstream_speed_kmh = 14.0f;
}

void WaterwayRoute::parse_osm_tags() {
    // 1. Waterway Type (River vs Canal)
    if (tags.count("waterway")) {
        std::string type = tags["waterway"];
        is_canal = (type == "canal" || type == "drain" || type == "ditch");
        if (is_canal) current_speed_kmh = 0.0f; // Canals have negligible current
    }

    // 2. CEMT Classification (The "Standard" sizes)
    // This overrides manual dimensions unless they are explicitly set.
    if (tags.count("CEMT")) {
        cemt_class = tags["CEMT"];
        // Approximate dimensions based on CEMT standards
        if (cemt_class == "I") {          // Peniche (Spit)
            max_draft_meters = 1.8f; beam_meters = 5.0f; air_draft_meters = 4.0f;
        } else if (cemt_class == "II") {  // Kampine
            max_draft_meters = 2.5f; beam_meters = 6.6f; air_draft_meters = 5.0f;
        } else if (cemt_class == "III") { // Dortmund-Ems
            max_draft_meters = 2.5f; beam_meters = 8.2f; air_draft_meters = 6.0f;
        } else if (cemt_class == "IV") {  // Rhine-Herne (Europaschiff)
            max_draft_meters = 2.5f; beam_meters = 9.5f; air_draft_meters = 7.0f;
        } else if (cemt_class == "Va") {  // Large Rhine
            max_draft_meters = 2.8f; beam_meters = 11.4f; air_draft_meters = 9.1f;
        } else if (cemt_class == "Vb") {  // Large Rhine (Convoy)
            max_draft_meters = 2.8f; beam_meters = 11.4f; air_draft_meters = 9.1f;
        } else if (cemt_class.find("VI") != std::string::npos) { // Arterial
            max_draft_meters = 4.5f; beam_meters = 22.8f; air_draft_meters = 9.1f;
        }
    }

    // 3. Explicit Overrides (Map data beats standards)
    if (tags.count("maxdraft")) {
        try { max_draft_meters = std::stof(tags["maxdraft"]); } catch(...) {}
    }
    if (tags.count("maxheight")) { // Bridge clearance
        try { air_draft_meters = std::stof(tags["maxheight"]); } catch(...) {}
    }

    // 4. Hazards (Dams, Locks, Rapids)
    if (tags.count("lock") || tags.count("weir")) {
        // These are points, usually handled by ChokePoints, 
        // but if tagged on a way, it implies restriction.
        navigability_status = 1; // Restricted speed
    }
}

void WaterwayRoute::update_metrics() {
    // 1. Effective Draft (Drought Calculation)
    // If the river is low (-1.5m), the available draft shrinks.
    // e.g., 2.5m normal draft - 1.5m low water = 1.0m actual.
    float effective_draft = max_draft_meters + water_level_stage;
    
    if (effective_draft < 1.0f) {
        navigability_status = 3; // Closed (Too shallow for commercial barges)
        hazard_type = "drought";
    } else if (effective_draft < 2.0f) {
        navigability_status = 1; // Light-loading only (Can't fill the barge)
        hazard_type = "low_water";
    }

    // 2. Ice Status
    if (is_frozen) {
        navigability_status = 3;
        hazard_type = "ice";
        upstream_speed_kmh = 0.0f;
        downstream_speed_kmh = 0.0f;
        return; // Route is dead
    }

    // 3. Current vs Engine Power
    // Standard Barge Engine Speed ~12-15 km/h relative to water.
    float engine_speed = 13.0f; 

    // Downstream: Engine + Current
    downstream_speed_kmh = engine_speed + current_speed_kmh;
    
    // Upstream: Engine - Current
    // If current is too strong (flooding), upstream travel becomes impossible.
    upstream_speed_kmh = engine_speed - current_speed_kmh;

    if (upstream_speed_kmh < 1.0f) {
        upstream_speed_kmh = 0.0f; // Current is too strong to navigate against
        hazard_type = "flood_current";
    }

    // 4. Civil Unrest / Blockade
    if (threat_level >= 5) {
        navigability_status = 3;
        hazard_type = "blockade";
        upstream_speed_kmh = 0.0f;
        downstream_speed_kmh = 0.0f;
    }
}