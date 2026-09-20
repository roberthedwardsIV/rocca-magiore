#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <pqxx/pqxx>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include "db_conn.hpp"

using json = nlohmann::json;

static std::string env_required(const char* key) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string();
}

// Credentials from environment (see .env.example)
const std::string DB_CONN = hippocampus_conn();
const std::string BSKY_PDS = "https://bsky.social";
const std::string BSKY_HANDLE = env_required("BSKY_HANDLE");
const std::string BSKY_PASSWORD = env_required("BSKY_PASSWORD");


// Helper function: writes API call contents into unified format for fetching
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}


// URL function: encodes our query for insertion into twitter url used at search time
std::string url_encode(CURL* curl, const std::string& value) {
    char* output = curl_easy_escape(curl, value.c_str(), value.length());
    if (output) {
        std::string result(output);
        curl_free(output);
        return result;
    }
    return "";
}


// Geolocator function: finds nearest city to signal triggering twitter search
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


// Authentication function: obtains BlueSky authentication token needed for Twitter search
std::string authenticate_and_get_token() {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string url = BSKY_PDS + "/xrpc/com.atproto.server.createSession";
    std::string response_buffer;

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
            std::cerr << "[TWITTER](AUTH ERR) Failed to parse token." << std::endl;
        }
    } else {
        std::cerr << "[TWITTER](AUTH ERR) Failed. HTTP: " << http_code << " | Resp: " << response_buffer << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return token;
}


// Twitter search function: constructs custom query based on signal type + location, then pulls 
// latest 25 tweets and sends to frontal_lobe via "twitter_stream_buffer" redis channel
void perform_twitter_search(redisContext* redis, json& task) {
    std::string access_token = authenticate_and_get_token();
    if (access_token.empty()) {
        std::cerr << "[TWITTER] Skipping search. Authentication failed." << std::endl;
        return;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string type = task.value("type", "unknown");
    std::string city = get_nearest_city(task["lat"], task["lon"]);
    
    if (city.empty()) {
        std::cout << "[TWITTER] No city found. Aborting." << std::endl;
        curl_easy_cleanup(curl);
        return;
    }

    std::string search_term = "";
    if (type == "earthquake") search_term = "earthquake " + city;
    else if (type == "wildfire") search_term = "wildfire " + city;
    // Able to add more event types here to properly construct tweet query
    else search_term = type + " " + city;

    std::cout << "[TWITTER] Authenticated Search: '" << search_term << "'" << std::endl;

    std::string encoded_query = url_encode(curl, search_term);
    std::string url = BSKY_PDS + "/xrpc/app.bsky.feed.searchPosts?q=" + encoded_query + "&limit=25";
    std::string response_buffer;
    
    struct curl_slist* headers = NULL;
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
            std::cout << "[TWITTER] Success. Pushed " << count << " posts." << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "[TWITTER](JSON ERR) " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[TWITTER](API ERR) HTTP: " << http_code << " | Resp: " << response_buffer << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}


// Main(): connects + listens to "twitter_recon_tasks" redis channel and calls perform_twitter_search()
// when prompted by new signal
int main() {
    std::cout << "[TWITTER] Authenticated Slow Stream Ingest Online." << std::endl;
    
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