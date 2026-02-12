#ifndef POWER_SUBSTATION_CHOKEPOINT_HPP
#define POWER_SUBSTATION_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class PowerSubstationChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL CAPACITY ---
    float max_voltage_kv;      // e.g., 500kV, 220kV
    float max_load_mva;        // MegaVolt-Amperes (Total Capacity)
    
    // --- DYNAMIC STATE ---
    float current_load_mva;    // Real-time demand
    float transformer_temp_c;  // >90C is dangerous, >110C is critical
    bool is_tripped;           // Circuit breaker status (Open = Blackout)
    bool is_maintenance;       // Scheduled downtime

    // Constructor
    PowerSubstationChokePoint(int id, std::string name, double lat, double lon, 
                              float voltage, float capacity_mva);

    // Calculates Power Availability (0.0 = Blackout, 1.0 = Stable)
    float calculate_throughput_modifier() override;

    // Listens for 'scada' (grid data) and 'thermal' (overheating)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif