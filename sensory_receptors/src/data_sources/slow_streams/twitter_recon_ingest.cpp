#include <iostream>
#include <string>
#include <vector>
#include <pqxx/pqxx>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

using json = nlohmann::json;

// --- CONFIGURATION ---
const std::string DB_CONN = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";

// HOST: The main server, not the public proxy
const std::string BSKY_PDS = "https://bsky.social"; 

// CREDENTIALS: YOU MUST SET THESE
// 1. Create a free account at bsky.app
// 2. Go to Settings > App Passwords > Add App Password
const std::string BSKY_HANDLE = "REMOVED"; 
const std::string BSKY_PASSWORD = "REMOVED"; 

// --- UTILITIES ---

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string url_encode(CURL* curl, const std::string& value) {
    char* output = curl_easy_escape(curl, value.c_str(), value.length());
    if (output) {
        std::string result(output);
        curl_free(output);
        return result;
    }
    return "";
}

std::string get_nearest_city(float lat, float lon) {
    try {
        pqxx::connection C(DB_CONN);
        if (!C.is_open()) return "";
        pqxx::work W(C);
        std::string sql = 
            "SELECT name FROM spatial_ref.world_cities "
            "WHERE population > 15000 "
            "ORDER BY coords <-> ST_SetSRID(ST_MakePoint(" + 
            std::to_string(lon) + "," + std::to_string(lat) + "), 4326) LIMIT 1;";
        pqxx::result R = W.exec(sql);
        if (!R.empty()) return R[0][0].as<std::string>();
    } catch (...) {}
    return "";
}

// --- AUTHENTICATION ---

std::string authenticate_and_get_token() {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string url = BSKY_PDS + "/xrpc/com.atproto.server.createSession";
    std::string response_buffer;

    // JSON Payload
    json payload;
    payload["identifier"] = BSKY_HANDLE;
    payload["password"] = BSKY_PASSWORD;
    std::string json_str = payload.dump();

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    std::string token = "";
    if (res == CURLE_OK && http_code == 200) {
        try {
            auto j = json::parse(response_buffer);
            token = j["accessJwt"];
        } catch (...) {
            std::cerr << "[AUTH ERR] Failed to parse token." << std::endl;
        }
    } else {
        std::cerr << "[AUTH ERR] Failed. HTTP: " << http_code << " | Resp: " << response_buffer << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return token;
}

// --- SEARCH EXECUTION ---

void perform_twitter_search(redisContext* redis, json& task) {
    // 1. Authenticate First (Get Fresh Token)
    // Since this is a "Slow Stream" (rare execution), logging in every time is safer/easier than managing expiry.
    std::string access_token = authenticate_and_get_token();
    if (access_token.empty()) {
        std::cerr << "[TWITTER RECON] Skipping search. Authentication failed." << std::endl;
        return;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string type = task.value("type", "unknown");
    std::string city = get_nearest_city(task["lat"], task["lon"]);
    
    if (city.empty()) {
        std::cout << "[TWITTER RECON] No city found. Aborting." << std::endl;
        curl_easy_cleanup(curl);
        return;
    }

    std::string search_term = "";
    if (type == "earthquake") search_term = "earthquake " + city;
    else if (type == "wildfire") search_term = "wildfire " + city;
    else search_term = type + " " + city;

    std::cout << "[TWITTER RECON] Authenticated Search: '" << search_term << "'" << std::endl;

    std::string encoded_query = url_encode(curl, search_term);
    std::string url = BSKY_PDS + "/xrpc/app.bsky.feed.searchPosts?q=" + encoded_query + "&limit=25";
    std::string response_buffer;
    
    struct curl_slist* headers = NULL;
    // CRITICAL: Add the Bearer Token
    std::string auth_header = "Authorization: Bearer " + access_token;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Rocco-Maggiore-Recon/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (res == CURLE_OK && http_code == 200) {
        try {
            json raw_response = json::parse(response_buffer);
            json packet;
            packet["task_id"] = task["task_id"]; 
            packet["type"] = type;               
            packet["search_term"] = city;
            packet["raw_data"] = raw_response; 

            std::string payload = packet.dump();
            redisCommand(redis, "LPUSH twitter_stream_buffer %s", payload.c_str());
            
            int count = 0;
            if(raw_response.contains("posts")) count = raw_response["posts"].size();
            std::cout << "[TWITTER RECON] Success. Pushed " << count << " posts." << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "[JSON ERR] " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[API ERR] HTTP: " << http_code << " | Resp: " << response_buffer << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

int main() {
    std::cout << "[TWITTER RECON] Authenticated Slow Stream Ingest Online." << std::endl;
    
    redisContext* sub = redisConnect("corpus_callosum", 6379);
    redisContext* pub = redisConnect("corpus_callosum", 6379);
    
    if (!sub || sub->err || !pub || pub->err) return 1;

    redisReply* reply;
    redisCommand(sub, "SUBSCRIBE twitter_recon_tasks");

    while (redisGetReply(sub, (void**)&reply) == REDIS_OK) {
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
            try {
                json task = json::parse(reply->element[2]->str);
                perform_twitter_search(pub, task);
            } catch (...) {}
        }
        freeReplyObject(reply);
    }
    return 0;
}