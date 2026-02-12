#include "DistributionHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

DistributionHub::DistributionHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "distribution", lat, lon) {
    
    // Default Infrastructure (Small Rural Depot)
    num_silos = 0;
    num_tanks = 0;
    open_yard_area_sqm = 2000.0f; // Small gravel lot
    warehouse_area_sqm = 0.0f;
    
    // Default Loading (Basic)
    weighbridges = 1;
    tipper_lanes = 1;
    loading_spouts = 0;
    has_rail_dump = false;

    // Default Storage (Generic Bulk)
    max_capacity_tons = 5000.0f;
    max_capacity_m3 = 3000.0f;
    current_inventory_tons = 0.0f;

    // Default Operations
    loading_rate_tph = 50.0f;     // Front-end loader speed
    unloading_rate_tph = 100.0f;  // Dump truck speed
    truck_turnaround_time_h = 0.5f; // 30 mins

    cargo_category = "dry_bulk";
    specific_commodity = "aggregate"; // Gravel/Sand default
}

void DistributionHub::parse_osm_tags() {
    // 1. Structure Detection (Silo vs Tank vs Yard)
    if (tags.count("man_made")) {
        std::string mm = tags["man_made"];
        if (mm == "silo") {
            num_silos = 1; // Default, usually clustered
            cargo_category = "dry_bulk";
            specific_commodity = "grain"; // Most common silo use
        } else if (mm == "storage_tank") {
            num_tanks = 1;
            cargo_category = "liquid_bulk";
            specific_commodity = "fuel";
        }
    }

    // 2. Commodity Specifics
    if (tags.count("content") || tags.count("product")) {
        std::string c = tags.count("content") ? tags["content"] : tags["product"];
        specific_commodity = c;
        
        if (c == "oil" || c == "diesel" || c == "gasoline") cargo_category = "liquid_bulk";
        else if (c == "grain" || c == "wheat" || c == "corn") cargo_category = "dry_bulk";
        else if (c == "coal" || c == "ore") cargo_category = "dry_bulk";
        else if (c == "cement") cargo_category = "dry_bulk";
    }

    // 3. Capacity Estimation (Geometry -> Volume)
    // Silos: Volume = Pi * r^2 * h
    if (num_silos > 0) {
        float height = 20.0f; // Default silo height
        float radius = 5.0f;
        if (tags.count("height")) height = std::stof(tags["height"]);
        
        // Total volume = Volume per silo * Count (OSM often maps cluster as one node)
        // Heuristic: If "silo" tag exists, assume at least 2000 m3 capacity
        max_capacity_m3 = 2000.0f * (num_silos > 0 ? num_silos : 1);
    }
    
    // Tanks: Similar geometry
    if (num_tanks > 0) {
        max_capacity_m3 = 5000.0f; // Tanks are usually bigger
    }

    // Yards: Area * Stack Height (approx 5m for coal/ore piles)
    if (tags.count("landuse") && tags["landuse"] == "industrial") {
        if (tags.count("area")) { // If pre-calculated
             open_yard_area_sqm = std::stof(tags["area"]);
        } else {
             open_yard_area_sqm = 10000.0f; // Fallback for industrial polygons
        }
        max_capacity_m3 += (open_yard_area_sqm * 5.0f);
    }

    // 4. Weight Calculation (Density Look-up)
    float density = 1.5f; // Default (Aggregate)
    if (specific_commodity == "grain") density = 0.75f; // Wheat
    else if (specific_commodity == "coal") density = 0.85f; // Bituminous
    else if (specific_commodity == "iron_ore") density = 2.5f; // Heavy!
    else if (specific_commodity == "fuel") density = 0.85f; // Diesel

    max_capacity_tons = max_capacity_m3 * density;

    // 5. Rail Interface
    if (tags.count("railway") && tags["railway"] == "spur") {
        has_rail_dump = true;
        unloading_rate_tph = 500.0f; // Rail dump is fast
    }
}

void DistributionHub::update_status() {
    // 1. Weighbridge Congestion
    // Weighbridge cycle time is fixed (~10 mins). Capacity = 6 trucks/hr per scale.
    float max_trucks_per_hour = weighbridges * 6.0f;
    
    // If we are at 90% capacity, throughput crawls
    if (current_inventory_tons / max_capacity_tons > 0.9f) {
        truck_turnaround_time_h = 2.0f; // Delays finding space to dump
    } else {
        truck_turnaround_time_h = 1.0f / max_trucks_per_hour; 
        if (truck_turnaround_time_h < 0.2f) truck_turnaround_time_h = 0.2f; // Min 12 mins
    }

    // 2. Loading Rate Constraints (Outbound)
    // If commodity is sticky/wet (e.g., wet coal), rates drop.
    // This would ideally check weather, but we use a simple factor here.
    float flow_efficiency = 1.0f;
    
    // Liquid pumps are consistent; Conveyors can jam.
    if (cargo_category == "dry_bulk") {
        loading_rate_tph = 200.0f * flow_efficiency; // Conveyor belt standard
    } else {
        loading_rate_tph = 150.0f; // Pump standard
    }
}

bool DistributionHub::can_accept_commodity(const std::string& commodity, bool requires_cover) const {
    // 1. Category Mismatch
    // Cannot put Oil in a Coal yard.
    // Simplistic string check - in real engine, use enums.
    if (cargo_category == "liquid_bulk" && commodity != "fuel" && commodity != "oil") return false;
    if (cargo_category == "dry_bulk" && (commodity == "fuel" || commodity == "oil")) return false;

    // 2. Storage Requirement Check
    if (requires_cover) {
        // Grain needs cover (Silo or Warehouse). Ore does not.
        if (num_silos == 0 && warehouse_area_sqm < 100.0f) {
            return false; // Only have open yard -> Reject grain
        }
    }

    return true;
}