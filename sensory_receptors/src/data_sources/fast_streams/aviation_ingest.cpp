#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>
#include <chrono>
#include <thread>
#include <ctime>

using json = nlohmann::json;

/**
* @brief  

*/

// ------------------------------------------------------------------------------------------------------------------------
// Name: WriteCallback() | Type: FUNCTION-UTIL | Params: contents (data), size, nmemb, and userp | Output: size * nmemb
// Purpose: piece together the chunks of data recieved from OpenSky into a unified data object
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}



// ------------------------------------------------------------------------------------------------------------------------
// Configuration Variables
const std::string CLIENT_ID = "REMOVED";
const std::string CLIENT_SECRET = "JF5DSUA7jjS2CQGy2G0u1R92ug8Vp87s";
const std::string TOKEN_URL = "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
std::string access_token = "";
auto token_expiry = std::chrono::steady_clock::now();




// Function to get a fresh OAuth2 Token (2025 Method)
bool refresh_token() {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string readBuffer;
    std::string postFields = "grant_type=client_credentials&client_id=" + CLIENT_ID + "&client_secret=" + CLIENT_SECRET;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "User-Agent: Rocco-Maggiore-Nexus/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, TOKEN_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && http_code == 200) {
        try {
            auto j = json::parse(readBuffer);
            access_token = j["access_token"];
            token_expiry = std::chrono::steady_clock::now() + std::chrono::seconds(1500);
            std::cout << "[AUTH] Success: Access Token Acquired." << std::endl;
            return true;
        } catch (...) { return false; }
    } else {
        std::cerr << "[AUTH ERR] HTTP: " << http_code << " | Response: " << readBuffer << std::endl;
        return false;
    }
}





void sync_flights(redisContext* redis) {
    // 1. Check/Refresh OAuth2 Token
    if (access_token == "" || std::chrono::steady_clock::now() >= token_expiry) {
        if (!refresh_token()) {
            std::cerr << "[ERR] Cannot sync flights without valid token." << std::endl;
            return;
        }
    }

    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string buf;
    // TARGET: Global API (Costs 4 credits, covers everything)
    std::string url = "https://opensky-network.org/api/states/all";
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
    headers = curl_slist_append(headers, "User-Agent: Rocco-Maggiore-Nexus/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    // 2. Execute Global Pulse
    if (curl_easy_perform(curl) == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 200 && !buf.empty() && buf[0] == '{') {
            try {
                auto j = json::parse(buf);
                if (j.contains("states") && !j["states"].is_null()) {
                    int count = 0;
                    for (auto& s : j["states"]) {
                        // Data extraction by index based on OpenSky API response schema
                        std::string icao = s[0].get<std::string>();
                        std::string call = s[1].is_null() ? "UNK" : s[1].get<std::string>();
                        
                        // Clean callsign strings (OpenSky often leaves trailing spaces)
                        call.erase(call.find_last_not_of(" \n\r\t") + 1);

                        double lon = s[5].is_null() ? 0 : s[5].get<double>();
                        double lat = s[6].is_null() ? 0 : s[6].get<double>();
                        double alt = s[7].is_null() ? 0 : s[7].get<double>();
                        double vel = s[9].is_null() ? 0 : s[9].get<double>();
                        double ver = s[11].is_null() ? 0 : s[11].get<double>();

                        if (lat == 0 || lon == 0) continue;

                        // 3. Update Redis Layer
                        redisCommand(redis, "SELECT 1");
                        std::string member = icao + ":" + call;
                        
                        // Geo-index for spatial queries
                        redisCommand(redis, "GEOADD global_sky %f %f %s", lon, lat, member.c_str());

                        // Real-time Hash for the Python Brain
                        std::string key = "flight_data:" + icao;
                        redisCommand(redis, "HSET %s lat %f lon %f alt %f vel %f v_rate %f ts %ld", 
                                     key.c_str(), lat, lon, alt, vel, ver, std::time(nullptr));
                        
                        // Expire after 5 mins to keep Redis clean if a plane leaves coverage
                        redisCommand(redis, "EXPIRE %s 300", key.c_str());
                        count++;
                    }
                    std::cout << "[PULSE] Global Sync: " << count << " aircraft updated." << std::endl;
                }
            } catch (const json::parse_error& e) {
                std::cerr << "[JSON ERR] Failed to parse: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "[API WARN] Global Pulse Failed | HTTP: " << http_code << std::endl;
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}




int main() {
    redisContext* redis = redisConnect("redis", 6379);
    if (redis == NULL || redis->err) {
        std::cerr << "[REDIS ERR] " << (redis ? redis->errstr : "Allocation error") << std::endl;
        return 1;
    }

    std::cout << "[AVIATION] Ingest Engine Online. Polling Global BBOXes...\n";

    while (true) {
        sync_flights(redis);
        std::this_thread::sleep_for(std::chrono::seconds(90));
    }

    redisFree(redis);
    return 0;
}