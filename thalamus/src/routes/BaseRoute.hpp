#ifndef BASE_ROUTE_HPP
#define BASE_ROUTE_HPP

#include <vector>
#include <string>
#include <map>
#include <memory>
#include "../chokepoints/BaseChokePoint.hpp"

// Simple coordinate struct for OSM geometry
struct GPSCoord {
    double lat;
    double lon;
};

class BaseRoute {
public:
    // --- IDENTITY ---
    long long osm_id;            // Unique OSM Identifier
    std::string name;            // e.g., "I-95", "Suez Canal", "Trans-Siberian"
    std::string type;            // "road", "rail", "maritime", "pipeline", "power", "air"

    // --- GEOMETRY ---
    std::vector<GPSCoord> geometry; // The physical path (nodes)

    // --- METADATA ---
    // Stores raw OSM tags (e.g., "maxspeed=120", "voltage=500000")
    // This allows subclasses to parse only what they need.
    std::map<std::string, std::string> tags;

    // --- CONNECTED INFRASTRUCTURE ---
    // Pointers to the specific ChokePoints that exist ALONG this route.
    // e.g., A 'RoadRoute' might contain a 'BridgeChokePoint' and a 'BorderChokePoint'.
    std::vector<std::shared_ptr<BaseChokePoint>> bottlenecks;

    // --- CONSTRUCTOR ---
    BaseRoute(long long id, std::string name, std::string type);
    virtual ~BaseRoute() = default;

    // --- UTILITIES ---
    void add_tag(const std::string& key, const std::string& value);
    void add_bottleneck(std::shared_ptr<BaseChokePoint> cp);
    
    // Calculates total physical length in km from geometry
    double get_length_km() const; 
};

#endif