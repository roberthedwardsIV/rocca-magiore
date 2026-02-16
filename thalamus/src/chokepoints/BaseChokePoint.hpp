#ifndef BASE_CHOKEPOINT_HPP
#define BASE_CHOKEPOINT_HPP

#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class BaseChokePoint {
public:
    int id;
    std::string name;
    std::string entity_type; // "bridge", "border", "tunnel", etc.
    
    // Spatial Location
    double latitude;
    double longitude;

    BaseChokePoint(int id, std::string name, std::string type, double lat, double lon) 
        : id(id), name(name), entity_type(type), latitude(lat), longitude(lon), last_update(0) {}
    
    virtual ~BaseChokePoint() = default;

    // --- PURE VIRTUAL INTERFACE ---
    // Every chokepoint must calculate its own flow modifier (0.0 to 1.0)
    // based on its own unique internal logic (Health? Politics? Weather?)
    virtual float calculate_throughput_modifier() = 0;

    // Every chokepoint interprets signals differently
    virtual void process_packet(const json& sig) = 0;
    
    // Every chokepoint exports its own unique state schema
    virtual json get_json_state() const = 0;

protected:
    mutable std::mutex choke_mutex;
    long long last_update;
};

#endif // BASE_CHOKEPOINT_HPP 