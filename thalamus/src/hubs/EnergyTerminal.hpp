#ifndef ENERGY_TERMINAL_HPP
#define ENERGY_TERMINAL_HPP

#include "BaseHub.hpp"
#include <string>

class EnergyTerminal : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE (Static) ---
    std::string commodity_type; // "lng", "crude_oil", "refined_product"
    std::string terminal_role;  // "export_liquefaction", "import_regasification", "storage_hub"
    
    int num_tanks;
    float max_storage_m3;       // Volumetric limit
    float max_flow_rate_m3h;    // Pump/Compressor limit (m3/hour)
    
    // --- LNG SPECIFIC PHYSICS ---
    float liquefaction_capacity_mtpa; // Million Tonnes Per Annum (Export limit)
    float regas_capacity_mmscfd;      // Million Standard Cubic Feet/Day (Import limit)
    float boil_off_rate_pct_day;      // Natural loss (0.05% - 0.15%)

    // --- DYNAMIC STATE ---
    float current_inventory_m3;
    float current_flow_rate;          // Actual flow (m3/h or mmscfd)
    float current_pressure_psi;       // Line pressure
    float active_berths;              // Ships currently loading/unloading

    // --- OPERATIONAL METRICS ---
    float utilization_rate;     // 0.0 - 1.0
    float efficiency_factor;    // Maintenance/Temperature impact
    bool is_maintenance;

    // Constructor
    EnergyTerminal(long long id, std::string name, double lat, double lon);

    // --- INTERFACE IMPLEMENTATION ---
    // Parses "man_made=storage_tank", "substance=gas", "function=terminal"
    void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) override; 
    
    // Calculates boil-off loss and flow constraints based on process type
    void update_metrics();

    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};

#endif