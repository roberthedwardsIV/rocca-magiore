#ifndef BASE_HUB_HPP
#define BASE_HUB_HPP

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class BaseHub {
public:
    long long osm_id;
    std::string name;
    std::string type;          
    double lat;
    double lon;

    BaseHub(long long id, std::string n, std::string t, double latitude, double longitude) 
        : osm_id(id), name(n), type(t), lat(latitude), lon(longitude), last_update(0) {}

    virtual ~BaseHub() = default;

    virtual void parse_osm_tags(const std::unordered_map<std::string, std::string>& tags) = 0;

    virtual void process_packet(const json& sig) = 0;

    virtual json get_json_state() const = 0;

    // Virtual hook for complex hub logic (overridden by Smelter/Port)
    virtual void update_status() {}

protected:
    mutable std::mutex hub_mutex; 
    long long last_update;        
    
    std::unordered_map<std::string, std::string> tags; 
};

#endif