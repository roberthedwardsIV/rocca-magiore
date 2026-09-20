#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <hiredis/hiredis.h>
#include <cstdlib>

using json = nlohmann::json;

// --- CONFIGURATION ---
const std::string FRED_API_KEY = std::getenv("FRED_API_KEY") ? std::getenv("FRED_API_KEY") : "";
const std::string REDIS_HOST = "corpus_callosum";
const int REDIS_PORT = 6379;


// FRED Series IDs
const std::string SERIES_RISK_FREE = "DGS10";           // US Treasury Security Market Yield at 10-Yr. Constant Maturity     
const std::string SERIES_CORP_SPREAD = "BAMLC0A0CM";    // ICE BofA US Corp Index Option-Adjusted Spread


// Helper for libcurl to write the callback needed for data fetching
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}


// Helper to fetch single series values from FRED
double fetch_fred_value(const std::string& series_id) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1.0;

    std::string readBuffer;
    std::string url = "https://api.stlouisfed.org/fred/series/observations?series_id=" + series_id + 
                      "&api_key=" + FRED_API_KEY + 
                      "&file_type=json&sort_order=desc&limit=1";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && http_code == 200) {
        try {
            auto j = json::parse(readBuffer);
            if (j.contains("observations") && !j["observations"].empty()) {
                std::string val_str = j["observations"][0]["value"];
                
                // FRED API returns "." strings for holidays/weekends/missing data
                if (val_str == "." || val_str.empty()) {
                    std::cout << "[FRED] Data missing/holiday for " << series_id << std::endl;
                    return -1.0; 
                }
                return std::stod(val_str);
            }
        } catch (const std::exception& e) {
            std::cerr << "[FRED] JSON Parse Error for " << series_id << ": " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[FRED] HTTP Error " << http_code << " fetching " << series_id << std::endl;
    }
    return -1.0;
}


// main(): runs fetch helper for risk_free_rate and corporate_spread every 60 minutes
int main() {
    std::cout << "[FRED] Service Online. Connecting to Redis..." << std::endl;

    redisContext* c = redisConnect(REDIS_HOST.c_str(), REDIS_PORT);
    if (c == NULL || c->err) {
        std::cerr << "[FRED] Redis Connection Error: " << (c ? c->errstr : "Allocation error") << std::endl;
        return 1;
    }

    while (true) {
        double rf_rate = fetch_fred_value(SERIES_RISK_FREE);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        double corp_spread = fetch_fred_value(SERIES_CORP_SPREAD);

        if (rf_rate > 0 && corp_spread > 0) {
            // Convert percent to decimal
            double rf_decimal = rf_rate / 100.0;
            double spread_decimal = corp_spread / 100.0;

            json payload;
            payload["entity_type"] = "macro_economic";
            payload["category"] = "rates";
            payload["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            payload["data"] = {
                {"risk_free_rate", rf_decimal},
                {"corporate_spread", spread_decimal},
                {"source", "FRED_API"}
            };

            std::string json_str = payload.dump();
            
            // Publish to Thalamus
            redisReply* reply = (redisReply*)redisCommand(c, "LPUSH raw_signals %s", json_str.c_str());
            if (reply) freeReplyObject(reply);

            std::cout << "[FRED] Updated WACC Baseline | RF: " << rf_rate << "% | Spread: " << corp_spread << "%" << std::endl;
        } else {
            std::cerr << "[FRED] Skipping update cycle due to missing API data." << std::endl;
        }

        // Macro data checked every 60 minutes
        std::this_thread::sleep_for(std::chrono::minutes(60));
    }

    redisFree(c);
    return 0;
}