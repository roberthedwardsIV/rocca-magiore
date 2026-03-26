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
int process_url(const std::string& url, redisContext* redis, bool silent_mode) {
    log_info("Fetching: " + url + (silent_mode ? " [INITIAL SNAPSHOT - SILENT]" : " [LIVE MODE]"));
    std::string csv_data = fetch_csv_data(url);
    if (csv_data.empty()) return 0;

    auto points = parse_firms_csv(csv_data);
    if (points.empty()) return 0;

    int new_pts = 0;
    int cached_pts = 0;

    for (const auto& p : points) {
        // Unique ID: Lat_Lon_Date_Time (NASA's specific detection event)
        std::string unique_id = std::to_string(p.lat) + "_" + std::to_string(p.lon) + "_" + p.acq_date + "_" + p.acq_time;
        std::string cache_key = "fire_seen:" + unique_id;

        // SETNX: 1 if new to our system, 0 if we've seen this detection before
        redisReply* reply = (redisReply*)redisCommand(redis, "SET %s 1 NX EX %d", cache_key.c_str(), CACHE_TTL_SECONDS);
        bool is_new_to_cache = (reply && reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
        if (reply) freeReplyObject(reply);

        if (!is_new_to_cache) {
            cached_pts++;
            continue;
        }

        // Only push to the Thalamus if we are NOT in the initial baseline fetch
        if (!silent_mode) {
            json j;
            j["source"] = "NASA_FIRMS";
            j["lat"] = p.lat;
            j["lon"] = p.lon;
            j["temp_k"] = p.temp_kelvin;
            j["frp"] = p.frp;
            j["acq_dt"] = p.acq_date + " " + p.acq_time;
            j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            
            redisCommand(redis, "LPUSH fire_stream_buffer %s", j.dump().c_str());
            new_pts++;
        } else {
            new_pts++; // Just counting for the log in silent mode
        }
    }

    log_info("Result: " + std::to_string(new_pts) + " cached as baseline | " + std::to_string(cached_pts) + " existing.");
    return points.size();
}

int main() {
    log_info("Booting NASA FIRMS Gateway (Smart Poll Mode)...");

    // 1. KEY LOADING (Moved outside the loop for efficiency)
    const char* env_key = std::getenv("NASA_FIRMS_KEY");
    if (!env_key) { 
        log_error("CRITICAL: NASA_FIRMS_KEY missing from environment."); 
        return 1; 
    }
    std::string api_key = trim_key(std::string(env_key));
    log_info("Loaded Key: " + api_key.substr(0, 4) + "....");

    // 2. REDIS CONNECT
    redisContext* redis = redisConnect(REDIS_HOST.c_str(), REDIS_PORT);
    if (!redis || redis->err) { 
        log_error("Redis Connection Failed: " + (redis ? std::string(redis->errstr) : "Memory error")); 
        return 1; 
    }

    // 3. CHECK INITIALIZATION STATE
    bool initialized = false;
    redisReply* init_check = (redisReply*)redisCommand(redis, "GET firms_initialized");
    if (init_check && init_check->type == REDIS_REPLY_STRING && std::string(init_check->str) == "1") {
        initialized = true;
        log_info("System already initialized in Redis. Resuming Live Mode.");
    }
    if (init_check) freeReplyObject(init_check);

    // 4. MAIN LOOP
    while (true) {
        // Construct URL using the pre-loaded api_key
        std::string url = "https://firms.modaps.eosdis.nasa.gov/api/area/csv/" + api_key + "/VIIRS_SNPP_NRT/world/1";
        
        if (!initialized) {
            log_info("ESTABLISHING BASELINE (System Zero). No signals will be generated...");
            process_url(url, redis, true); // SILENT MODE
            
            // Persist the initialized state to Redis
            redisCommand(redis, "SET firms_initialized 1");
            initialized = true;
            log_info("Baseline established. Entering Live Monitoring...");
        } else {
            process_url(url, redis, false); // LIVE MODE
        }

        log_info("Sleeping 10 minutes...");
        std::this_thread::sleep_for(std::chrono::minutes(10));
    }

    redisFree(redis);
    return 0;
}