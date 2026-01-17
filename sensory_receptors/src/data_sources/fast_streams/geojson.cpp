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

using json = nlohmann::json;
using namespace sw::redis;

/**
* Source: GeoJSON (USGS Earthquake Alerts)
* Brief: we poll the last hour alert dataset every minute as it gets updated.
*        With each new earthquake posted, we trigger a signal.
*/


// Structure for calculated seismic signals
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
            {"mag", data.at("mag")}
        };
        return j.dump();
    }
};


// Helper function to do raw API fetching
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}


// Helper function that requests data from USGS GeoJSON
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


// ---------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------
int main() {
    const std::string url = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_hour.geojson";
    std::string last_processed_id = "";

    // Connect to Redis
    auto redis = Redis("tcp://corpus_callosum:6379");

    std::cout << "[GEOJSON] Earthquake Monitor Started. Connecting to Redis & Postgres..." << std::endl;

    while (true) {
        std::string raw_data = fetchUSGSData(url);

        if (!raw_data.empty()) {
            try {
                auto data = json::parse(raw_data);
                auto features = data["features"];

                if (!features.empty()) {
                    std::string current_newest_id = features[0]["id"];

                    if (current_newest_id != last_processed_id) {
                        for (const auto& feature : features) {
                            std::string id = feature["id"];
                            if (id == last_processed_id) break;

                            auto props = feature["properties"];
                            auto geometry = feature["geometry"]["coordinates"];

                            float mag = props["mag"];
                            float lon = geometry[0];
                            float lat = geometry[1];
                            long long ms_since_epoch = props["time"];

                            // Create Signal Packet
                            SignalPacket signal;
                            signal.entity_id = id;
                            signal.data["lat"] = lat;
                            signal.data["lon"] = lon;
                            signal.data["mag"] = mag;

                            signal.timestamp = ms_since_epoch;

                            // Push to Redis
                            redis.lpush("raw_signals", signal.to_json_str());

                            std::cout << "[GeoJSON] Earthquake Alert at: " << std::fixed << std::setprecision(2) << lat;
                            std::cout << ", " << std::fixed << std::setprecision(2) << lon;
                            std::cout << std::endl;
                        }
                        last_processed_id = current_newest_id;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "JSON processing error: " << e.what() << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}