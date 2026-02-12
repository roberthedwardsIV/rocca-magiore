#ifndef DISTRIBUTION_HUB_HPP
#define DISTRIBUTION_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class DistributionHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE ---
    int num_silos;              // Vertical storage (Grain, Cement)
    int num_tanks;              // Liquid storage (Fuel, Chemicals)
    float open_yard_area_sqm;   // Flat storage (Coal, Ore, Scrap, Lumber)
    float warehouse_area_sqm;   // Covered flat storage (Fertilizer, Steel Coils)
    
    // --- LOADING INFRASTRUCTURE ---
    int weighbridges;           // Critical bottleneck for bulk trucks
    int tipper_lanes;           // Dump truck unloading slots
    int loading_spouts;         // Gravity feed (Grain/Cement)
    bool has_rail_dump;         // Bottom-dump rail siding (Rotary or Trestle)

    // --- STORAGE CAPACITY (Mass & Volume) ---
    float max_capacity_tons;    // The hard limit for heavy bulk (Ore, Steel)
    float max_capacity_m3;      // The hard limit for light bulk (Grain, Woodchips)
    float current_inventory_tons;

    // --- OPERATIONAL METRICS ---
    float loading_rate_tph;     // Tons Per Hour (Conveyor/Pump speed)
    float unloading_rate_tph;   // Tons Per Hour (Dump speed)
    float truck_turnaround_time_h; // Weigh-in -> Dump -> Weigh-out

    // --- COMMODITY SPECIFICS ---
    // "dry_bulk", "liquid_bulk", "break_bulk" (Steel/Lumber), "neo_bulk"
    std::string cargo_category;   
    std::string specific_commodity; // "grain", "coal", "fuel", "aggregate"

    // Constructor
    DistributionHub(long long id, std::string name, double lat, double lon);

    // Parses "industrial=depot", "man_made=silo", "content=coal"
    void parse_osm_tags() override;

    // Updates throughput based on active conveyors/pumps and weighbridge queues
    void update_status() override;
    
    // Returns true if the facility is compatible with the cargo form factor
    // e.g., A Silo cannot accept Steel Coils; An Open Yard cannot accept Wheat (Rain).
    bool can_accept_commodity(const std::string& commodity_type, bool requires_cover) const;
};

#endif