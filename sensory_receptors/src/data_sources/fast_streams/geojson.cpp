#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sw/redis++/redis++.h> 
#include <unordered_set>

using json = nlohmann::json;
using namespace sw::redis;
 

// Structure for calculated seismic signals to be sent to raw_signals
struct SignalPacket {
    std::string entity_id;;
    std::string entity_type = "earthquake";
    long long timestamp;
    float reliability_noise = 0.1;
    std::map<std::string, float> data;

    std::string to_json_str() const {
        json j;
        j["entity_id"] = entity_id;
        j["entity_type"] = entity_type;
        j["timestamp"] = timestamp;
        j["reliability_noise"] = reliability_noise;
        j["data"] = {
            {"lat", data.at("lat")}, 
            {"lon", data.at("lon")}, 
            {"mag", data.at("mag")},
            {"depth", data.at("depth")}
        };
        return j.dump();
    }
};


// Helper function: writes API call contents into unified format for fetching
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}


// Helper function: requests data from USGS GeoJSON (last hour - all earthquakes)
std::string fetchUSGSData(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if(res != CURLE_OK) return "";
    }
    return readBuffer;
}


// Main(): runs the fetchUSGSData() every minute and sends signal packets to "raw_signals" redis channel for any new earthquakes found
int main() {
    const std::string url = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson";
    std::unordered_set<std::string> seen_ids;
    bool is_initial_run = true;
    
    auto redis = Redis("tcp://corpus_callosum:6379");

    std::cout << "[geojson] Earthquake Monitor Started. Connecting to Redis & Postgres..." << std::endl;

    while (true) {
        std::string raw_data = fetchUSGSData(url);

        if (!raw_data.empty()) {
            try {
                auto data = json::parse(raw_data);
                auto features = data["features"];
                
                std::unordered_set<std::string> current_feed_ids;

                if (!features.empty()) {
                    for (const auto& feature : features) {
                        std::string id = feature["id"];
                        current_feed_ids.insert(id);

                        // If ID hasn't been seen before
                        if (seen_ids.find(id) == seen_ids.end()) {
                            
                            // Only trigger signals if this isn't the first boot
                            if (!is_initial_run) {
                                auto props = feature["properties"];
                                auto geometry = feature["geometry"]["coordinates"];

                                float mag = props["mag"];
                                float lon = geometry[0];
                                float lat = geometry[1];
                                float depth = geometry[2];
                                long long ms_since_epoch = props["time"];

                                // Ignore intensity since it takes USGS days to get accurate survey
                                SignalPacket signal;
                                signal.entity_id = id;
                                signal.data["lat"] = lat;
                                signal.data["lon"] = lon;
                                signal.data["mag"] = mag;
                                signal.data["depth"] = depth;
                                signal.timestamp = ms_since_epoch;

                                redis.lpush("raw_signals", signal.to_json_str());
                                redis.publish("raw_signals", signal.to_json_str());

                                std::cout << "[geojson] Earthquake Alert at: " << std::fixed << std::setprecision(2) << lat;
                                std::cout << ", " << std::fixed << std::setprecision(2) << lon;
                                std::cout << std::endl;
                            }
                        }
                    }
                    // Update cache for the next cycle
                    seen_ids = current_feed_ids;
                    is_initial_run = false;
                }
            } catch (const std::exception& e) {
                std::cerr << "[geojson] (ERR) JSON processing error: " << e.what() << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}