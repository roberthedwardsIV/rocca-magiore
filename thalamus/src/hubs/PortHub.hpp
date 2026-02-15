#ifndef PORT_HUB_HPP
#define PORT_HUB_HPP

#include "BaseHub.hpp"
#include <string>

class PortHub : public BaseHub {
public:
    // --- PHYSICAL INFRASTRUCTURE (Static) ---
    int number_of_berths;       
    float max_draft_meters;     
    float total_quay_length_m;  
    bool has_rail_connection;   
    bool has_ro_ro_ramp;        

    // --- STORAGE CAPACITY (Three Modes) ---
    // 1. Containers (TEU)
    int max_storage_teu;        
    int current_storage_teu;
    int reefer_plugs;           

    // 2. Dry/Liquid Bulk (Raw Commodities)
    float max_bulk_storage_tons;     
    float current_bulk_storage_tons;

    // 3. Break Bulk (Finished Metals / Project Cargo) [ADDED]
    float max_break_bulk_storage_m2; // Covered/Open storage area for metals
    float current_break_bulk_tons;   // Inventory of Coils/Cathodes

    // --- OPERATIONAL METRICS ---
    float gate_throughput_vph;  // Truck gate speed
    float loading_rate_tph;     // Effective loading speed (varies by cargo type)
    float average_dwell_time_h; 
    float congestion_level;     

    // --- TYPE SPECIFICS ---
    // "container", "bulk", "break_bulk", "oil", "general"
    std::string port_type;

    // Constructor
    PortHub(long long id, std::string name, double lat, double lon);

    // --- INTERFACE IMPLEMENTATION ---
    // Parses tags to distinguish Bulk Terminals vs. General Cargo (Metal) Ports
    void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) override; 
    
    // Updates throughput: Metal loading (Crane lifts) is slower than Bulk (Conveyor)
    void update_metrics();

    // Strict Overrides
    void process_packet(const json& sig) override;
    json get_json_state() const override;

    // Helper: Returns max vessel class (e.g., "Capesize")
    std::string get_max_vessel_class() const;
};

#endif