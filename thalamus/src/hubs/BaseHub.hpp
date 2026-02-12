#ifndef BASE_HUB_HPP
#define BASE_HUB_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "../chokepoints/BaseChokePoint.hpp"

class BaseHub {
public:
    // --- IDENTITY & LOCATION ---
    long long osm_id;
    std::string name;
    std::string type;       // "port", "airport", "railyard", "substation", "plant"
    double latitude;
    double longitude;

    // --- OSM RAW DATA ---
    std::map<std::string, std::string> tags;

    // --- CONTAINED CHOKE POINTS ---
    // The specific bottlenecks inside this hub (e.g., specific Cranes, Transformers)
    std::vector<std::shared_ptr<BaseChokePoint>> facilities;

    // --- CONSTRUCTOR ---
    BaseHub(long long id, std::string n, std::string t, double lat, double lon)
        : osm_id(id), name(n), type(t), latitude(lat), longitude(lon) {}

    virtual ~BaseHub() = default;

    // --- VIRTUAL INTERFACE ---
    // Forces subclasses to define their own parsing logic
    virtual void parse_osm_tags() = 0;

    // Forces subclasses to calculate their own "health" or "throughput"
    // e.g., Port calculates TEU/hr; Substation calculates MW capacity.
    virtual void update_status() = 0;

    // --- UTILITY ---
    void add_tag(const std::string& key, const std::string& value) {
        tags[key] = value;
    }

    void add_facility(std::shared_ptr<BaseChokePoint> facility) {
        facilities.push_back(facility);
    }
};

#endif