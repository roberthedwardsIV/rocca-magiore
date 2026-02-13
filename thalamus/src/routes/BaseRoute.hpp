#ifndef BASE_ROUTE_HPP
#define BASE_ROUTE_HPP

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <cmath>
#include <nlohmann/json.hpp>
#include "../chokepoints/BaseChokePoint.hpp"

using json = nlohmann::json;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct GPSCoord {
    double lat;
    double lon;
};

class BaseRoute {
public:
    long long osm_id;
    std::string name;
    std::string type;
    std::vector<GPSCoord> geometry;
    std::map<std::string, std::string> tags;
    std::vector<std::shared_ptr<BaseChokePoint>> bottlenecks;

    BaseRoute(long long id, std::string name, std::string type) 
        : osm_id(id), name(name), type(type), last_update(0) {}
    
    virtual ~BaseRoute() = default;

    void add_tag(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(route_mutex);
        tags[key] = value;
    }

    void add_bottleneck(std::shared_ptr<BaseChokePoint> cp) {
        std::lock_guard<std::mutex> lock(route_mutex);
        bottlenecks.push_back(cp);
    }

    double get_length_km() const {
        double total_km = 0.0;
        if (geometry.size() < 2) return 0.0;
        const double R = 6371.0; 
        for (size_t i = 0; i < geometry.size() - 1; ++i) {
            double dLat = (geometry[i+1].lat - geometry[i].lat) * M_PI / 180.0;
            double dLon = (geometry[i+1].lon - geometry[i].lon) * M_PI / 180.0;
            double a = std::sin(dLat/2) * std::sin(dLat/2) +
                       std::cos(geometry[i].lat * M_PI / 180.0) * std::cos(geometry[i+1].lat * M_PI / 180.0) *
                       std::sin(dLon/2) * std::sin(dLon/2);
            double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1-a));
            total_km += R * c;
        }
        return total_km;
    }

    virtual void process_packet(const json& sig) = 0;
    virtual json get_json_state() const = 0;

protected:
    mutable std::mutex route_mutex;
    long long last_update;
};

#endif