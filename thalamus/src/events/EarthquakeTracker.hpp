#ifndef EARTHQUAKE_TRACKER_HPP
#define EARTHQUAKE_TRACKER_HPP

#include "BaseEvent.hpp"
#include <mutex>
#include <vector>
#include <algorithm>

struct RawPacket {
    long long timestamp;
    json data;
    float reliability;
};

class EarthquakeTracker : public BaseEvent {
public:
    struct EarthquakeVector {
        float lat, lon;
        float magnitude;
        float intensity;
        long long event_time;
        long long update_time;
        float zr_score;
        float uncertainty;    
        float process_noise;  
    };

    EarthquakeTracker(const json& initial_sig);

    void process_packet(const json& sig) override;
    float calculate_zr_score() override;
    
    float get_lat() const override { return current_state.lat; }
    float get_lon() const override { return current_state.lon; }
    long long get_start_time() const override { return current_state.event_time; }

    bool is_stale(long long current_time) const override;
    long long get_last_update_time() const override;

    json to_json() const override;

private:
    EarthquakeVector current_state;
    std::vector<RawPacket> history_log; 

    void apply_signal_to_state(const RawPacket& pkt);

    void full_recalculate();
};

#endif