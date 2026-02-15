#ifndef DISTRIBUTION_HUB_HPP
#define DISTRIBUTION_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class DistributionHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE (Static) ---
    int num_silos;              // Vertical storage (Grain, Cement)
    int num_tanks;              // Liquid storage (Fuel, Chemicals)
    float open_yard_area_sqm;   // Flat storage (Coal, Ore, Scrap)
    float warehouse_area_sqm;   // Covered storage (Cathodes, Coils, Ingots)
    
    // --- LOADING INFRASTRUCTURE (Static) ---
    int weighbridges;           // Bottleneck for trucks
    int tipper_lanes;           // Dump truck slots
    int loading_spouts;         // Gravity feed (Grain)
    int gantry_cranes;          // Overhead cranes (Critical for Metal Coils/Slabs)
    bool has_rail_dump;         // Bottom-dump rail capability

    // --- STORAGE CAPACITY (Dynamic Limits) ---
    float max_capacity_tons;    // Mass limit
    float max_capacity_m3;      // Volume limit
    
    // --- DYNAMIC STATE ---
    float current_inventory_tons;
    float loading_rate_tph;     // Outbound speed
    float unloading_rate_tph;   // Inbound speed
    float truck_turnaround_time_h; 

    // --- COMMODITY SPECIFICS ---
    // "dry_bulk" (Ore), "liquid_bulk" (Fuel), "break_bulk" (Finished Metal)
    std::string cargo_category;     
    std::string specific_commodity; // "copper_cathode", "steel_coil", "ore"

    // Constructor
    DistributionHub(long long id, std::string name, double lat, double lon);

    // --- INTERFACE IMPLEMENTATION ---
    void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) override; 
    
    // Updates throughput: Break Bulk (Metals) moves slower than Conveyor Bulk
    void update_metrics();

    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;

    // Helper: Compatibility Check (e.g. Cathodes require cover)
    bool can_accept_commodity(const std::string& commodity_type, bool requires_cover) const;
};

#endif