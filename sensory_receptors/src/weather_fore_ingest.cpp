#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>
#include <curl/curl.h>
#include <hiredis/hiredis.h>

struct MemoryBuffer { std::vector<char> data; };

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    auto* mem = static_cast<MemoryBuffer*>(userp);
    mem->data.insert(mem->data.end(), (char*)contents, (char*)contents + totalSize);
    return totalSize;
}

bool try_url(const std::string& url) {
    MemoryBuffer buffer;
    CURL* curl = curl_easy_init();
    if(!curl) return false;

    std::cout << "[WEATHER_INGEST] Checking: " << url << std::endl;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L); // Increased timeout for large files

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if(code == 200 && buffer.data.size() > 100000000) { // Safety check: file must be > 100MB
        std::cout << "[SUCCESS] Found Data! Size: " << buffer.data.size() << " bytes." << std::endl;
        redisContext* c = redisConnect("redis", 6379);
        if (c && !c->err) {
            // Use XADD to push the binary as a single stream message
            redisCommand(c, "XADD weather_stream * data %b", buffer.data.data(), buffer.data.size());
            std::cout << "[SYSTEM] Data pushed to Redis Stream." << std::endl;
            redisFree(c);
            return true;
        }
    }
    return false;
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    std::cout << "[SYSTEM] Starting Aggressive Backfill with Redis Streams..." << std::endl;

    while(true) {
        bool global_success = false;
        for (int h = 0; h <= 48; h += 6) {
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now - std::chrono::hours(h));
            std::tm* utc = std::gmtime(&t);
            
            int run_h = (utc->tm_hour / 6) * 6;
            char d[9], r[3];
            std::strftime(d, sizeof(d), "%Y%m%d", utc);
            std::snprintf(r, sizeof(r), "%02d", run_h);
            
            std::string date_s(d), run_s(r);
            std::vector<std::string> urls = {
                "https://data.ecmwf.int/forecasts/" + date_s + "/" + run_s + "z/ifs/0p25/oper/" + date_s + run_s + "0000-0h-oper-fc.grib2",
                "https://data.ecmwf.int/forecasts/" + date_s + "/" + run_s + "z/ifs/0p25/oper/" + date_s + run_s + "00-0h-oper-fc.grib2"
            };

            for(const auto& url : urls) {
                if(try_url(url)) { global_success = true; break; }
            }
            if(global_success) break;
        }

        if (global_success) {
            std::cout << "[SYSTEM] Data Sync Complete. Sleeping 4 hours..." << std::endl;
            std::this_thread::sleep_for(std::chrono::hours(4));
        } else {
            std::cout << "[SYSTEM] Absolute Failure. Retrying in 1m..." << std::endl;
            std::this_thread::sleep_for(std::chrono::minutes(1));
        }
    }
    return 0;
}