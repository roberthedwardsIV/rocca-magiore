#ifndef WILDFIRE_TRACKER_HPP
#define WILDFIRE_TRACKER_HPP

#include "BaseEvent.hpp"
#include <mutex>
#include <algorithm>

class WildfireTracker : public BaseEvent {
public:
    struct FireState {
        float lat;
        float lon;
        float max_frp;       // Maximum Fire Radiative Power seen
        float current_frp;   // Most recent reading
        float dist_km;       // Distance to the threatened asset
        long long start_time;
        long long last_update;
        float containment_index; // 0.0 (Wild) -> 1.0 (Out)
    };

    WildfireTracker(const json& initial_sig);
    void process_packet(const json& sig) override;
    float calculate_zr_score() override;

    // Getters for BaseEvent interface
    float get_lat() const override { return current_state.lat; }
    float get_lon() const override { return current_state.lon; }
    long long get_start_time() const override { return current_state.start_time; }
    long long get_last_update_time() const override { return current_state.last_update; }
    
    // Fires are considered "stale" faster than earthquakes if satellite passes miss them
    bool is_stale(long long current_time) const override;
    json to_json() const override;

private:
    FireState current_state;
};

#endif