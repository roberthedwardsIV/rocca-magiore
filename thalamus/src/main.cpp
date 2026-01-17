#include <iostream>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <hiredis/hiredis.h>      
#include <nlohmann/json.hpp>      
#include "EarthquakeTracker.hpp"
#include "DatabaseManager.hpp"
#include <cmath>

#define EARTH_RADIUS_KM 6371.0

using json = nlohmann::json;

// Helper to get current system time in ms
long long get_current_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Helper haversine function
float calculate_distance(float lat1, float lon1, float lat2, float lon2) {
    float dLat = (lat2 - lat1) * M_PI / 180.0;
    float dLon = (lon2 - lon1) * M_PI / 180.0;

    float a = sin(dLat / 2) * sin(dLat / 2) +
              cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
              sin(dLon / 2) * sin(dLon / 2);
    
    float c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return EARTH_RADIUS_KM * c;
}

// Global Registry
std::unordered_map<std::string, std::shared_ptr<BaseEvent>> registry;
std::mutex registry_mutex;


// Search function (looks up coordinates + event_time to find existing vectors)
std::string find_matching_event(const json& sig) {
    // If packet lacks coordinates, we can't fuzzy search
    if (!sig["data"].contains("lat") || !sig["data"].contains("lon")) return "";

    float new_lat = sig["data"]["lat"];
    float new_lon = sig["data"]["lon"];
    long long new_time = sig["timestamp"];

    const float DISTANCE_THRESHOLD_KM = 150.0; 
    const long long TIME_THRESHOLD_MS = 180000; // Adjusted to match your 180s comment

    std::lock_guard<std::mutex> lock(registry_mutex);

    for (auto const& [id, tracker] : registry) {
        if (tracker->entity_type == "earthquake") {
            float dist = calculate_distance(new_lat, new_lon, tracker->get_lat(), tracker->get_lon());
            long long time_diff = std::abs(new_time - tracker->get_start_time());

            if (dist < DISTANCE_THRESHOLD_KM && time_diff < TIME_THRESHOLD_MS) {
                return id;
            }
        }
    }
    return "";
}


// Dispatcher
/**
* Brief: this will listen for our main data feeds posting signals to redis, 
* and file them into the proper exisiting tracker vector (if applicable),
* or create a new one if this is our first signal for an entity.
*/
void dispatch_signal(const json& sig) {
    // Extract station prefix (e.g., "II_JZAX")
    std::string id = sig["entity_id"];
    std::string station_prefix = (id.find_last_of('_') != std::string::npos) 
                                 ? id.substr(0, id.find_last_of('_')) 
                                 : id;

    std::lock_guard<std::mutex> lock(registry_mutex);

    std::string existing_id = "";
    float new_lat = sig["data"].value("lat", 0.0f);
    float new_lon = sig["data"].value("lon", 0.0f);
    long long new_time = sig["timestamp"];

    for (auto const& [reg_id, tracker] : registry) {
        // FALLBACK 1: Does the station prefix match exactly? 
        // (This catches rapid-fire updates from the same sensor)
        if (reg_id.find(station_prefix) != std::string::npos) {
            existing_id = reg_id;
            break;
        }

        // FALLBACK 2: Spatial/Temporal Match
        float dist = calculate_distance(new_lat, new_lon, tracker->get_lat(), tracker->get_lon());
        long long time_diff = std::abs(new_time - tracker->get_start_time());

        if (dist < 150.0f && time_diff < 180000) {
            existing_id = reg_id;
            break;
        }
    }

    if (existing_id != "") {
        registry[existing_id]->process_packet(sig);
    } 
    else {
        // Double check DB
        json archived = query_database_for_event(id, new_lat, new_lon, new_time);
        if (!archived.is_null()) {
            registry[id] = std::make_shared<EarthquakeTracker>(archived);
            registry[id]->process_packet(sig);
        } else {
            // Truly new birth
            std::cout << "[THALAMUS] Spawning new tracker for: " << id << std::endl;
            registry[id] = std::make_shared<EarthquakeTracker>(sig);
        }
    }
}

// Function that will "clean up" old signals without updates for 24-hours
void run_reaper() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(10));
        long long now = get_current_ms();

        std::lock_guard<std::mutex> lock(registry_mutex);
        
        auto it = registry.begin();
        while (it != registry.end()) {
            if (it->second->is_stale(now)) {
                
                // 1. ARCHIVE: Save the final state (and history log) to DB
                json final_state = it->second->to_json();
                save_to_database(final_state); 

                std::cout << "[REAPER] Archived and deleted: " << it->first << std::endl;
                
                // 2. DELETE FROM MEMORY
                it = registry.erase(it); 
            } else {
                ++it;
            }
        }
    }
}


// -----------------------------------------------------------------------------
int main() {
    // 1. Initialize DB Connection
    if (!init_database()) {
        std::cerr << "[FATAL] Could not connect to TimescaleDB\n";
        return 1;
    }

    // 2. Launch Reaper Thread
    std::thread reaper_thread(run_reaper);
    reaper_thread.detach();

    // 3. Redis connection
    redisContext *c = redisConnect("corpus_callosum", 6379);
    if (c == NULL || c->err) {
        std::cerr << "[FATAL] Redis connection error\n";
        return 1;
    }
    

    std::cout << "[THALAMUS] Supervisor Online. Parallel Processing Active.\n";

    while(true) {
        redisReply *reply = (redisReply*)redisCommand(c, "BRPOP raw_signals 0");
        
        if (reply != nullptr && reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
            try {
                json sig = json::parse(reply->element[1]->str);
                
                std::cout << "[THALAMUS] Signal Received: " << sig["entity_id"] << std::endl;
                dispatch_signal(sig);

            } catch (const std::exception& e) {
                std::cerr << "[JSON ERR] Failed to route signal: " << e.what() << std::endl;
            }
        }
        freeReplyObject(reply);
    }

    redisFree(c);
    return 0;
}