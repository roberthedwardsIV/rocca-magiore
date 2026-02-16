#ifndef POWER_SUBSTATION_CHOKEPOINT_HPP
#define POWER_SUBSTATION_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class PowerSubstationChokePoint : public BaseChokePoint {
public:
    // --- ELECTRICAL INFRASTRUCTURE (Static) ---
    float max_voltage_kv;       // Rated Voltage
    float max_capacity_mva;     // Rated Apparent Power
    float thermal_time_constant_h; // Heating lag (e.g. 4.0 hours)

    // --- DYNAMIC STATE ---
    float current_load_mva;     // Actual flow
    float core_temp_c;          // Transformer hot spot temp
    float ambient_temp_c;       // Cooling baseline
    
    // --- OPERATIONAL FLAGS ---
    bool is_tripped;            // Breaker open (Blackout)
    bool is_maintenance;        // Scheduled work

    // --- CALCULATED METRICS ---
    float load_factor;          // Load / Capacity
    float voltage_pu;           // Per-Unit Voltage (1.0 = Nominal)

    // Constructor
    PowerSubstationChokePoint(int id, std::string name, double lat, double lon, 
                              float voltage, float capacity_mva);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates flow modifier based on:
    // 1. Trip Status (0.0 if tripped)
    // 2. Thermal Derating (Reduce flow if overheating to avoid trip)
    // 3. Voltage Stability (Collapsing voltage reduces effective MW transfer)
    float calculate_throughput_modifier() override;

    // Listens for 'scada' (load), 'weather' (temp), 'protection' (trip)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    void update_thermal_model();
};

#endif