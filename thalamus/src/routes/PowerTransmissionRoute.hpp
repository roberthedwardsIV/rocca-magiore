#ifndef POWER_TRANSMISSION_ROUTE_HPP
#define POWER_TRANSMISSION_ROUTE_HPP

#include "BaseRoute.hpp"

class PowerTransmissionRoute : public BaseRoute {
public:
    // Static
    float voltage_kv;                   // Voltage of Line (kV)
    float frequency_hz;                 // Line frequency (Hz)
    int circuits;                       // N-1 Redundancy check
    int cables_per_phase;               // Bundle conductors 
    bool is_hvdc;                       // Long distance bulk transmission
    bool is_underground;                // Cable vs Overhead 
    bool is_dedicated_feed;             // True if line solely powers a Mine/Smelter
    bool has_arc_furnace_load;          // Causes flicker/harmonics 
    float base_power_factor;            // 0.85 (Inductive Mine) vs 0.95 (Grid Standard)

    // Dynamic
    float current_load_mw;              // Real Power (mW)
    float current_mvar;                 // Reactive Power (mW) 
    float conductor_temp_c;             // Sag limit monitor (C)
    bool is_tripped;                    // Protection relay status (C)
    float ambient_temp_c;               // Weather temp (C)

    // Calculated
    float thermal_limit_mva;            // Wire Heating Limit (MVA)
    float effective_mw_capacity;        // Usable power (mW)
    float surge_impedance_loading_mw;   // Stability limit for long lines (mW)

    PowerTransmissionRoute(long long id, std::string name);

    void parse_osm_tags();     
    void update_metrics();     
    
    void process_packet(const json& sig) override;
    json get_json_state() const override;
};

#endif