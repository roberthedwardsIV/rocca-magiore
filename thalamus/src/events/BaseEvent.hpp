#ifndef BASE_EVENT_HPP
#define BASE_EVENT_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <mutex>

using json = nlohmann::json;

class BaseEvent {
public:
    virtual ~BaseEvent() = default;
    
    virtual void process_packet(const json& sig) = 0;
    virtual float calculate_zr_score() = 0;

    virtual float get_lat() const = 0;
    virtual float get_lon() const = 0;
    virtual long long get_start_time() const = 0;

    virtual long long get_last_update_time() const = 0;
    virtual bool is_stale(long long current_time) const = 0;
    virtual json to_json() const = 0;
    
    std::string entity_id;   // "EQ_2026_01"
    std::string entity_type; // "earthquake"

protected:
    std::mutex data_mutex;
};

#endif