#ifndef PUMPING_STATION_CHOKEPOINT_HPP
#define PUMPING_STATION_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class PumpingStationChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL CAPACITY ---
    float max_pressure_psi;    // Maximum operating pressure
    float max_flow_rate_bpd;   // Barrels per day (Oil) or m3/day (Water)
    
    // --- DYNAMIC STATE ---
    float current_pressure_psi;// Real-time sensor reading
    int active_pumps;          // Number of pumps online
    int total_pumps;           // Total installed pumps
    bool is_leaking;           // Integrity failure

    // Constructor
    PumpingStationChokePoint(int id, std::string name, double lat, double lon, 
                             float max_psi, float max_flow);

    // Calculates Flow based on Pressure + Active Pumps
    float calculate_throughput_modifier() override;

    // Listens for 'pressure' (PSI) and 'integrity' (leaks)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;
};

#endif