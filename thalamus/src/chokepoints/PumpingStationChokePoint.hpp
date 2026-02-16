#ifndef PUMPING_STATION_CHOKEPOINT_HPP
#define PUMPING_STATION_CHOKEPOINT_HPP

#include "BaseChokePoint.hpp"

class PumpingStationChokePoint : public BaseChokePoint {
public:
    // --- PHYSICAL CAPACITY (Static) ---
    float max_pressure_psi;    // Maximum Allowable Operating Pressure (MAOP)
    float max_flow_rate_bpd;   // Barrels per day (Nameplate)
    int total_pumps;           // Installed units
    float required_npsh_psi;   // Net Positive Suction Head (Min inlet pressure)

    // --- DYNAMIC STATE ---
    float discharge_pressure_psi; // Outlet pressure
    float suction_pressure_psi;   // Inlet pressure (Critical for cavitation)
    int active_pumps;             // Currently running
    float fluid_viscosity_cst;    // Affects pump efficiency (1.0 = Water, 30.0 = Heavy Crude)
    
    // --- OPERATIONAL FLAGS ---
    bool is_leaking;           // Integrity failure
    bool is_cavitating;        // Physics failure (Suction pressure too low)
    bool is_maintenance;       // Scheduled downtime

    // --- CALCULATED METRICS ---
    float hydraulic_efficiency; // 0.0 - 1.0 (Based on viscosity & wear)

    // Constructor
    PumpingStationChokePoint(int id, std::string name, double lat, double lon, 
                             float max_psi, float max_flow);

    // --- INTERFACE IMPLEMENTATION ---
    
    // Calculates flow modifier based on:
    // 1. Leak Status (Safety Shutdown)
    // 2. Cavitation (Physics Throttle)
    // 3. Active Pumps vs Total
    float calculate_throughput_modifier() override;

    // Listens for 'scada' (pressure/pumps), 'integrity' (leak), 'fluid' (viscosity)
    void process_packet(const json& sig) override;
    
    json get_json_state() const override;

private:
    void check_cavitation_physics();
};

#endif