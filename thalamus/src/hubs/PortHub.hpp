#ifndef PORT_HUB_HPP
#define PORT_HUB_HPP

#include "BaseHub.hpp"
#include <vector>

class PortHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE ---
    int number_of_berths;       // How many ships can dock simultaneously
    float max_draft_meters;     // Limits the size of vessels (Panamax vs Post-Panamax)
    float total_quay_length_m;  // Total docking frontage
    bool has_rail_connection;   // "railway=spur" or similar
    bool has_ro_ro_ramp;        // Roll-on/Roll-off (Cars/Trucks)

    // --- STORAGE CAPACITY (The "Buffer") ---
    int max_storage_teu;        // Yard capacity in Twenty-foot Equivalent Units
    int current_storage_teu;    // Current inventory
    int reefer_plugs;           // Capacity for refrigerated containers
    
    // --- OPERATIONAL METRICS ---
    float gate_throughput_vph;  // Trucks per hour (Gate capacity)
    float average_dwell_time_h; // How long cargo sits (Congestion indicator)
    float congestion_level;     // 0.0 (Empty) -> 1.0 (Gridlock)
    
    // --- TYPE SPECIFICS ---
    // "container", "bulk", "oil", "fishing", "naval"
    std::string port_type; 

    // Constructor
    PortHub(long long id, std::string name, double lat, double lon);

    // Parses "harbour:category", "mooring:depth", "capacity:teu"
    // Infers capacity from land area if explicit tags are missing.
    void parse_osm_tags() override;

    // Aggregates throughput from attached Cranes + Gate efficiency
    // Updates congestion_level based on current_storage / max_storage
    void update_status() override;
    
    // Returns the max vessel class this port can handle (e.g., "New Panamax")
    std::string get_max_vessel_class() const;
};

#endif