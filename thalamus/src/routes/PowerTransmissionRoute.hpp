#ifndef POWER_TRANSMISSION_ROUTE_HPP
#define POWER_TRANSMISSION_ROUTE_HPP

#include "BaseRoute.hpp"

class PowerTransmissionRoute : public BaseRoute {
public:
    // --- ELECTRICAL SPECIFICATIONS ---
    float voltage_kv;          // 110kV, 220kV, 380kV, 765kV (Primary capacity driver)
    float frequency_hz;        // 50Hz (EU/Asia) vs 60Hz (US)
    int circuits;              // Number of independent 3-phase circuits (usually 1 or 2)
    int cables_per_phase;      // Bundle conductors (1-4) reduce resistance
    bool is_hvdc;              // High Voltage Direct Current (Special handling)
    bool is_underground;       // Cable (Lower thermal limit) vs Overhead Line

    // --- DYNAMIC STATE ---
    float current_load_mw;     // Real power flow
    float reactive_load_mvar;  // Reactive power (voltage support)
    float conductor_temp_c;    // Temperature of the wire (affects sag)
    bool is_tripped;           // Protection relay trip (Overload/Fault)

    // --- OPERATIONAL LIMITS ---
    float thermal_limit_mw;    // Max continuous rating (MVA ~ MW for high PF)
    float surge_impedance_loading_mw; // Stability limit for long lines

    // Constructor
    PowerTransmissionRoute(long long id, std::string name);

    // Parses "voltage", "frequency", "cables", "circuits"
    void parse_osm_tags();

    // Calculates Thermal Limit based on Voltage * Circuits
    // Adjusts for temperature derating
    void update_metrics();
};

#endif