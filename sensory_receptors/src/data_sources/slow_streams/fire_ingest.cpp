#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <algorithm>

using json = nlohmann::json;

// --- CONFIGURATION ---
const std::string REDIS_HOST = "corpus_callosum";
const int REDIS_PORT = 6379;
const int CACHE_TTL_SECONDS = 86400; // 24 Hours

struct FirePoint {
    double lat;
    double lon;
    double temp_kelvin;
    double frp;
    std::string confidence;
    std::string acq_time;
    std::string acq_date;
};

// --- LOGGING HELPER ---
void log_info(const std::string& msg) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout << "[FIRE INGEST][" << std::put_time(std::localtime(&now), "%T") << "] " << msg << std::endl;
}

void log_error(const std::string& msg) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cerr << "[FIRE INGEST][" << std::put_time(std::localtime(&now), "%T") << "][ERR] " << msg << std::endl;
}

// --- TRIM HELPER ---
std::string trim_key(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return str;
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// --- CURL WRITE CALLBACK ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}

// --- FETCH CSV ---
std::string fetch_csv_data(const std::string& target_url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string buffer;
    curl_easy_setopt(curl, CURLOPT_URL, target_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L); // 90s timeout for large files
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RoccoMaggiore/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        log_error("Fetch Failed. HTTP: " + std::to_string(http_code));
        if (buffer.size() < 200) log_error("API Response: " + buffer);
        return "";
    }
    return buffer;
}

// --- PARSER ---
std::vector<FirePoint> parse_firms_csv(const std::string& csv_data) {
    std::vector<FirePoint> points;
    std::stringstream ss(csv_data);
    std::string line;
    std::getline(ss, line); // Skip Header

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string cell;
        std::vector<std::string> cols;
        while (std::getline(ls, cell, ',')) cols.push_back(cell);

        if (cols.size() < 13) continue;
        try {
            FirePoint fp;
            fp.lat = std::stod(cols[0]);
            fp.lon = std::stod(cols[1]);
            fp.temp_kelvin = std::stod(cols[2]);
            fp.acq_date = cols[5];
            fp.acq_time = cols[6];
            fp.confidence = cols[9];
            fp.frp = std::stod(cols[12]);
            points.push_back(fp);
        } catch (...) { continue; }
    }
    return points;
}

// --- CORE PROCESSING LOGIC ---
int process_url(const std::string& url, redisContext* redis) {
    log_info("Fetching: " + url);
    std::string csv_data = fetch_csv_data(url);
    
    if (csv_data.empty()) return 0;

    auto points = parse_firms_csv(csv_data);
    if (points.empty()) {
        log_info("Valid CSV received, but contained 0 points.");
        return 0;
    }

    int new_pts = 0;
    int dup_pts = 0;

    for (const auto& p : points) {
        // Unique ID: Lat_Lon_Time
        std::string unique_id = std::to_string(p.lat) + "_" + std::to_string(p.lon) + "_" + p.acq_date + "_" + p.acq_time;
        std::string cache_key = "fire_seen:" + unique_id;

        // 1. Check Redis Cache (SETNX = Set if Not Exists)
        // Returns 1 if new, 0 if exists
        redisReply* reply = (redisReply*)redisCommand(redis, "SET %s 1 NX EX %d", cache_key.c_str(), CACHE_TTL_SECONDS);
        
        bool is_new = false;
        if (reply) {
            if (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK") is_new = true; // For simple SET
            if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1) is_new = true; // For SETNX
            freeReplyObject(reply);
        }

        if (!is_new) {
            dup_pts++;
            continue;
        }

        // 2. Push New Fire to Stream
        json j;
        j["source"] = "NASA_FIRMS";
        j["lat"] = p.lat;
        j["lon"] = p.lon;
        j["temp_k"] = p.temp_kelvin;
        j["frp"] = p.frp;
        j["conf"] = p.confidence;
        j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::string payload = j.dump();
        redisCommand(redis, "LPUSH fire_stream_buffer %s", payload.c_str());
        new_pts++;
    }

    log_info("Batch Result: " + std::to_string(points.size()) + " total | " + std::to_string(new_pts) + " new | " + std::to_string(dup_pts) + " duplicates.");
    return points.size();
}

int main() {
    log_info("Booting NASA FIRMS Gateway (Smart Poll Mode)...");

    // 1. KEY LOADING
    const char* env_key = std::getenv("NASA_FIRMS_KEY");
    if (!env_key) { log_error("CRITICAL: NASA_FIRMS_KEY missing."); return 1; }
    std::string api_key = trim_key(std::string(env_key));
    log_info("Loaded Key: " + api_key.substr(0, 4) + "....");

    // 2. REDIS CONNECT
    redisContext* redis = redisConnect(REDIS_HOST.c_str(), REDIS_PORT);
    if (!redis || redis->err) { log_error("Redis Connection Failed."); return 1; }

    // 3. MAIN LOOP
    while (true) {
        // STRATEGY: Try 1 Day. If empty, try 2 Days.
        std::string url_1day = "https://firms.modaps.eosdis.nasa.gov/api/area/csv/" + api_key + "/VIIRS_SNPP_NRT/world/1";
        
        int count = process_url(url_1day, redis);

        if (count == 0) {
            log_info("Day 1 was empty (likely latency). Fallback to Day 2...");
            std::string url_2day = "https://firms.modaps.eosdis.nasa.gov/api/area/csv/" + api_key + "/VIIRS_SNPP_NRT/world/2";
            process_url(url_2day, redis);
        }

        log_info("Sleeping 10 minutes...");
        std::this_thread::sleep_for(std::chrono::minutes(10));
    }

    redisFree(redis);
    return 0;
}