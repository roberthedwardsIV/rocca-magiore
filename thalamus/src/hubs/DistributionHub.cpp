#include "DistributionHub.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

DistributionHub::DistributionHub(long long id, std::string name, double lat, double lon)
    : BaseHub(id, name, "distribution", lat, lon) {
    
    // Default Infrastructure
    num_silos = 0;
    num_tanks = 0;
    open_yard_area_sqm = 2000.0f; 
    warehouse_area_sqm = 0.0f;
    
    // Loading Equipment
    weighbridges = 1;
    tipper_lanes = 1;
    loading_spouts = 0;
    gantry_cranes = 0;
    has_rail_dump = false;

    // Capacity & State
    max_capacity_tons = 5000.0f;
    max_capacity_m3 = 3000.0f;
    current_inventory_tons = 0.0f;
    
    // Operations
    loading_rate_tph = 50.0f;     
    unloading_rate_tph = 50.0f;  
    truck_turnaround_time_h = 0.5f; 

    cargo_category = "dry_bulk";
    specific_commodity = "aggregate"; 
}

void DistributionHub::parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) {
    // 1. Structure Detection
    if (tags.count("man_made")) {
        std::string mm = tags.at("man_made");
        if (mm == "silo") {
            num_silos = 1; 
            cargo_category = "dry_bulk";
            specific_commodity = "grain"; 
        } else if (mm == "storage_tank") {
            num_tanks = 1;
            cargo_category = "liquid_bulk";
            specific_commodity = "fuel";
        } else if (mm == "gantry_crane" || mm == "crane") {
            gantry_cranes = 1;
        }
    }

    if (tags.count("building") && tags.at("building") == "warehouse") {
        // Warehouse usually implies finished goods or weather-sensitive bulk
        if (tags.count("area")) try { warehouse_area_sqm = std::stof(tags.at("area")); } catch(...) {}
        else warehouse_area_sqm = 2000.0f; // Default warehouse size
    }

    // 2. Commodity Specifics & Categorization
    if (tags.count("content")) specific_commodity = tags.at("content");
    else if (tags.count("product")) specific_commodity = tags.at("product");

    // Logic: Identify Finished Metals vs Raw Bulk
    if (specific_commodity == "copper_cathode" || specific_commodity == "steel_coil" || 
        specific_commodity == "aluminum_ingot" || specific_commodity == "metal_products") {
        cargo_category = "break_bulk";
    }
    else if (specific_commodity == "oil" || specific_commodity == "diesel" || specific_commodity == "gasoline") {
        cargo_category = "liquid_bulk";
    }
    else {
        // Default to dry bulk (ore, coal, grain)
        cargo_category = "dry_bulk";
    }

    // 3. Capacity Estimation
    max_capacity_m3 = 0.0f;

    // A. Silos
    if (num_silos > 0) max_capacity_m3 += (2000.0f * num_silos);
    
    // B. Tanks
    if (num_tanks > 0) max_capacity_m3 += (5000.0f * num_tanks);

    // C. Yards (Raw Ore)
    if (tags.count("landuse") && tags.at("landuse") == "industrial") {
        if (tags.count("area")) try { open_yard_area_sqm = std::stof(tags.at("area")); } catch(...) {}
        else open_yard_area_sqm = 10000.0f;
        max_capacity_m3 += (open_yard_area_sqm * 5.0f); // 5m stack height
    }

    // D. Warehouses (Finished Metals)
    if (warehouse_area_sqm > 0) {
        max_capacity_m3 += (warehouse_area_sqm * 4.0f); // 4m stack height for pallets/coils
    }

    // 4. Weight Calculation (Density Look-up)
    float density = 1.5f; // Aggregate default
    if (cargo_category == "break_bulk") {
        // Finished metals are very dense but have air gaps in stacking
        density = 4.0f; // Stacked Steel/Copper
    } else if (specific_commodity == "iron_ore") {
        density = 2.5f;
    } else if (specific_commodity == "fuel") {
        density = 0.85f;
    }

    max_capacity_tons = max_capacity_m3 * density;

    // 5. Rail Interface
    if (tags.count("railway") && tags.at("railway") == "spur") {
        has_rail_dump = true;
    }
    
    update_metrics();
}

void DistributionHub::update_metrics() {
    // 1. Weighbridge Congestion
    float max_trucks_per_hour = (float)weighbridges * 6.0f;
    
    if (max_capacity_tons > 0 && (current_inventory_tons / max_capacity_tons) > 0.9f) {
        truck_turnaround_time_h = 2.0f; // Yard is full, hard to maneuver
    } else {
        truck_turnaround_time_h = 1.0f / max_trucks_per_hour;
        if (truck_turnaround_time_h < 0.2f) truck_turnaround_time_h = 0.2f;
    }

    // 2. Throughput Physics by Category
    float flow_efficiency = 1.0f; 

    if (cargo_category == "liquid_bulk") {
        // Pumps are fast and consistent
        loading_rate_tph = 150.0f;
        unloading_rate_tph = 150.0f;
    } 
    else if (cargo_category == "break_bulk") {
        // Finished Metals: Handling is discrete (Crane/Forklift moves 1 unit at a time)
        // Gantry cranes boost speed significantly compared to forklifts
        float base_rate = (gantry_cranes > 0) ? 80.0f : 30.0f;
        loading_rate_tph = base_rate * flow_efficiency;
        unloading_rate_tph = base_rate * flow_efficiency;
    }
    else {
        // Dry Bulk: Conveyors and Gravity are very fast
        loading_rate_tph = 200.0f * flow_efficiency;
        // Rail dumps are incredibly fast for unloading
        unloading_rate_tph = has_rail_dump ? 500.0f : 100.0f;
    }
}

void DistributionHub::process_packet(const json& sig) {
    std::lock_guard<std::mutex> lock(hub_mutex);
    std::string category = sig.value("category", "none");

    // Inventory Updates
    if (category == "logistics" || category == "inventory") {
        if (sig.contains("current_tons")) {
            current_inventory_tons = sig["current_tons"].get<float>();
        } else if (sig.contains("fill_pct")) {
            current_inventory_tons = max_capacity_tons * sig["fill_pct"].get<float>();
        }
    }
    
    // Operational Status (Equipment Failure)
    else if (category == "maintenance" || category == "mechanical") {
        if (sig.contains("active") && sig["active"].get<bool>()) {
            // If cranes break, Break Bulk stops. If conveyors break, Dry Bulk stops.
            loading_rate_tph *= 0.1f; 
            unloading_rate_tph *= 0.1f;
        }
    }

    last_update = sig.value("timestamp", 0LL);
    update_metrics();
}

bool DistributionHub::can_accept_commodity(const std::string& commodity, bool requires_cover) const {
    // 1. Physical State Mismatch
    if (cargo_category == "liquid_bulk" && commodity != "fuel" && commodity != "oil") return false;
    
    // 2. Metal Compatibility
    // If this is a Break Bulk yard (Cathodes), it won't take loose Ore.
    if (cargo_category == "break_bulk" && (commodity == "ore" || commodity == "coal")) return false;

    // 3. Covered Storage Requirement (High Value Metals)
    // Copper Cathodes / Steel Coils usually require cover to prevent oxidation/theft.
    if (requires_cover) {
        if (warehouse_area_sqm < 100.0f && num_silos == 0) return false;
    }
    return true;
}

json DistributionHub::get_json_state() const {
    std::lock_guard<std::mutex> lock(hub_mutex);
    return {
        {"id", osm_id},
        {"type", type},
        {"name", name},
        {"inventory_tons", current_inventory_tons},
        {"max_capacity_tons", max_capacity_tons},
        {"commodity", specific_commodity},
        {"category", cargo_category},
        {"turnaround_time_h", truck_turnaround_time_h},
        {"loading_rate_tph", loading_rate_tph},
        {"has_cranes", (gantry_cranes > 0)},
        {"last_update", last_update}
    };
}