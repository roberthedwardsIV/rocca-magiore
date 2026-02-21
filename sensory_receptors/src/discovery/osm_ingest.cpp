#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <sstream>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

using json = nlohmann::json;

// --- CONFIGURATION ---
const std::string DB_CONN = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";
const std::string OVERPASS_URL = "https://overpass-api.de/api/interpreter";
const int BATCH_SIZE = 250;

// --- GLOBAL STATS ---
std::atomic<int> stats_db_errors{0};
std::atomic<int> stats_saved{0};

// --- HELPER: CURL WRITER WITH HEARTBEAT ---
struct FetchContext {
    std::string* buffer;
    size_t last_log_size = 0;
    std::string phase_name;
    std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    FetchContext* ctx = static_cast<FetchContext*>(userp);
    
    try {
        ctx->buffer->append((char*)contents, total_size);
    } catch (const std::bad_alloc& e) {
        std::cerr << "\n[OSM INGEST][" << ctx->phase_name << "][CRITICAL] Out of Memory (OOM) during download! Buffer size: " 
                  << (ctx->buffer->size() / 1024 / 1024) << " MB" << std::endl;
        return 0; // Abort cURL
    }

    // Heartbeat: Log every 10 MB downloaded
    if (ctx->buffer->size() - ctx->last_log_size > 10 * 1024 * 1024) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ctx->start_time).count();
        std::cout << "   -> [" << ctx->phase_name << "] Streaming... " 
                  << (ctx->buffer->size() / 1024 / 1024) << " MB | Elapsed: " << elapsed << "s" << std::endl;
        ctx->last_log_size = ctx->buffer->size();
    }
    return total_size;
}

// --- HELPER: EXECUTE OVERPASS QUERY ---
std::string fetch_overpass_data(const std::string& query, const std::string& phase_name) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[OSM INGEST][" << phase_name << "] Failed to initialize cURL." << std::endl;
        return "";
    }

    std::string response_buffer;
    FetchContext ctx = {&response_buffer, 0, phase_name, std::chrono::steady_clock::now()};

    char* encoded_query = curl_easy_escape(curl, query.c_str(), query.length());
    std::string url = OVERPASS_URL + "?data=" + std::string(encoded_query);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RoccoMaggiore-AI-Nexus/1.0");
    
    // Hardened Network Settings
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);         // 10 min max total time per sector
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);   // 30 seconds to establish connection
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);     
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);

    auto start = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(curl);
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - ctx.start_time).count();
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        std::cerr << "   -> [CURL ERROR] " << curl_easy_strerror(res) << " after " << duration << "s." << std::endl;
    } else {
        if (http_code == 429) {
            std::cerr << "   -> [RATE LIMITED] Overpass rejected request (Too Many Requests). Waiting 60s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(60));
            response_buffer = ""; // Clear bad payload
        } else if (http_code >= 500 && http_code <= 599) {
            // THE FIX: Intercept 504 Gateway Timeouts and 502 Bad Gateways quietly
            std::cerr << "   -> [HTTP " << http_code << "] Overpass API Server Error (Overloaded). Suppressing HTML dump." << std::endl;
            response_buffer = ""; // Clear bad HTML payload to trigger our clean retry loop
        } else if (http_code != 200) {
            // Fallback for other errors: Sanitize newlines to prevent terminal spam
            std::string snippet = response_buffer.length() > 50 ? response_buffer.substr(0, 50) + "..." : response_buffer;
            std::replace(snippet.begin(), snippet.end(), '\n', ' ');
            std::cerr << "   -> [HTTP " << http_code << "] Failed. Response: " << snippet << std::endl;
            response_buffer = "";
        } else {
            std::cout << "   -> [SUCCESS] Downloaded " << std::fixed << std::setprecision(2) 
                      << (response_buffer.size() / 1024.0 / 1024.0) << " MB in " << duration << "s." << std::endl;
        }
    }
    
    curl_free(encoded_query);
    curl_easy_cleanup(curl);
    return response_buffer;
}

// --- HELPER: TAG PARSING ---
bool has_tag_value(const json& tags, const std::string& key, const std::string& partial) {
    if (tags.contains(key)) {
        std::string val = tags[key].get<std::string>();
        return val.find(partial) != std::string::npos;
    }
    return false;
}

std::string get_tag(const json& tags, const std::string& key, const std::string& fallback = "") {
    return tags.contains(key) ? tags[key].get<std::string>() : fallback;
}

// --- DB HANDLER ---
class DatabaseHandler {
    pqxx::connection m_db;
    pqxx::work* m_work = nullptr;
    int m_counter = 0;

public:
    DatabaseHandler() : m_db(DB_CONN) {
        if (!m_db.is_open()) throw std::runtime_error("DB Connection Failed");
        
        // SELF-HEALING SCHEMA
        try {
            pqxx::work W(m_db);
            W.exec("ALTER TABLE assets ADD COLUMN IF NOT EXISTS metadata JSONB DEFAULT '{}'::jsonb;");
            W.commit();
        } catch (const std::exception& e) {
            std::cerr << "[OSM INGEST] DB Schema warning: " << e.what() << std::endl;
        }

        reset_transaction();
        
        // 1. Restore the $2 placeholder for 'type'
        m_db.prepare("insert_asset", 
            "INSERT INTO assets (name, type, commodity_types, geom, source, last_update, op_health, metadata) "
            "VALUES ($1, $2, $3, ST_SetSRID(ST_MakePoint($4, $5), 4326), 'OSM_INGEST', 1, 1.0, $6) "
            "ON CONFLICT (name) DO UPDATE SET metadata = EXCLUDED.metadata, type = EXCLUDED.type");
        
        m_db.prepare("insert_hub",   
            "INSERT INTO supply_hubs (osm_id, name, type, geom, capacity_rating, last_updated) "
            "VALUES ($1, $2, $3, ST_SetSRID(ST_MakePoint($4, $5), 4326), $6, NOW()) ON CONFLICT (osm_id) DO UPDATE SET last_updated = NOW()");
        
        m_db.prepare("insert_route", 
            "INSERT INTO supply_lines (line_id, name, type, geom, state_data, last_update) "
            "VALUES ($1, $2, $3, ST_SetSRID(ST_GeomFromText($4), 4326), $5, 1) ON CONFLICT (line_id) DO UPDATE SET last_update = 1");
        
        m_db.prepare("insert_choke", 
            "INSERT INTO supply_chokepoints (name, type, geom, max_weight_tons, structural_health, last_updated) "
            "VALUES ($1, $2, ST_Centroid(ST_SetSRID(ST_GeomFromText($3), 4326)), $4, 1.0, NOW()) ON CONFLICT DO NOTHING");
    }

    ~DatabaseHandler() { 
        if (m_work) {
            try { m_work->commit(); } catch (...) {}
            delete m_work;
        }
    }

    void reset_transaction() {
        if (m_work) delete m_work;
        m_work = new pqxx::work(m_db);
        m_counter = 0;
    }

    void execute_asset(const std::string& name, const std::string& type, const std::string& comms, double lon, double lat, const std::string& metadata_json) {
        try {
            m_work->exec_prepared("insert_asset", name, type, comms, lon, lat, metadata_json);
            stats_saved++;
            commit_if_needed();
        } catch (const std::exception& e) { 
            stats_db_errors++; 
            reset_transaction(); 
        }
    }

    void execute_hub(long long osm_id, const std::string& name, const std::string& type, double lon, double lat, float cap) {
        try {
            m_work->exec_prepared("insert_hub", osm_id, name, type, lon, lat, cap);
            stats_saved++;
            commit_if_needed();
        } catch (...) { stats_db_errors++; reset_transaction(); }
    }

    void execute_route(long long osm_id, const std::string& name, const std::string& type, const std::string& wkt, const std::string& state_json) {
        try {
            m_work->exec_prepared("insert_route", osm_id, name, type, wkt, state_json);
            stats_saved++;
            commit_if_needed();
        } catch (...) { stats_db_errors++; reset_transaction(); }
    }

    void execute_choke(const std::string& name, const std::string& type, const std::string& wkt, float weight) {
        try {
            m_work->exec_prepared("insert_choke", name, type, wkt, weight);
            stats_saved++;
            commit_if_needed();
        } catch (...) { stats_db_errors++; reset_transaction(); }
    }
    
    void force_commit() {
        if (m_counter > 0 && m_work) {
            m_work->commit();
            reset_transaction();
        }
    }

private:
    void commit_if_needed() {
        if (++m_counter >= BATCH_SIZE) {
            m_work->commit();
            reset_transaction();
        }
    }
};

// --- LOGIC GATES ---

void process_element_assets_hubs(const json& elem, DatabaseHandler& db) {
    if (!elem.contains("tags")) return;
    auto tags = elem["tags"];
    long long id = elem["id"].get<long long>();

    double lat = 0, lon = 0;
    if (elem.contains("center")) {
        lat = elem["center"]["lat"]; lon = elem["center"]["lon"];
    } else if (elem.contains("lat") && elem.contains("lon")) {
        lat = elem["lat"]; lon = elem["lon"];
    } else return;

    std::string name = get_tag(tags, "name:en");
    if (name.empty()) {
        name = get_tag(tags, "name", "Unknown");
    }
    std::string landuse = get_tag(tags, "landuse");
    std::string industrial = get_tag(tags, "industrial");
    std::string resource = get_tag(tags, "resource");
    std::string man_made = get_tag(tags, "man_made");
    std::string power = get_tag(tags, "power");
    std::string railway = get_tag(tags, "railway");

    // --- OSINT EXTRACTION ---
    json metadata = json::object();
    if (tags.contains("operator")) metadata["operator"] = tags["operator"];
    if (tags.contains("brand")) metadata["brand"] = tags["brand"];
    if (tags.contains("company")) metadata["company"] = tags["company"];
    if (tags.contains("wikidata")) metadata["wikidata"] = tags["wikidata"];
    std::string metadata_str = metadata.dump();

    // 1. Assets
    if (landuse == "quarry" || industrial == "mine" || industrial == "mining") {
        std::string comms = resource.empty() ? "{Unknown}" : "{" + resource + "}";
        db.execute_asset(name, "mine", comms, lon, lat, metadata_str);
    }
    else if (man_made == "offshore_platform") {
        // Force the commodity to 'oil' so synapse_builder maps it to Energy Futures
        std::string comms = resource.empty() ? "{oil}" : "{" + resource + "}";
        db.execute_asset(name, "mine", comms, lon, lat, metadata_str);
    }
    else if (industrial == "refinery" || industrial == "oil" || industrial == "chemical") {
        db.execute_asset(name, "refinery", "{}", lon, lat, metadata_str);
    }
    
    // 2. Hubs
    else if (power == "plant") {
        db.execute_hub(id, name, "power_plant", lon, lat, 1.0f); 
    }
    else if (industrial == "port" || get_tag(tags, "harbour") == "yes") {
        db.execute_hub(id, name, "port", lon, lat, 1.0f); 
    }
    else if (railway == "yard") {
        db.execute_hub(id, name, "rail_node", lon, lat, 0.8f); 
    }
    else if (power == "substation") {
        db.execute_hub(id, name, "substation", lon, lat, 0.5f); 
    }
    else if (get_tag(tags, "building") == "warehouse" || industrial == "logistics" || industrial == "factory") {
        db.execute_hub(id, name, "distribution", lon, lat, 0.3f); 
    }
    else if (man_made == "storage_tank") {
        db.execute_hub(id, name, "energy_terminal", lon, lat, 0.8f); 
    }
    else if (get_tag(tags, "aeroway") == "aerodrome") {
        db.execute_hub(id, name, "airport", lon, lat, 1.0f); 
    }
}

void process_element_routes_chokes(const json& elem, DatabaseHandler& db) {
    if (elem["type"] != "way" || !elem.contains("tags") || !elem.contains("geometry")) return;
    
    auto tags = elem["tags"];
    long long osm_id = elem["id"].get<long long>();
    long long id = osm_id % 9000000000000000000LL; 
    std::string name = get_tag(tags, "name", "Unknown Route");

    std::stringstream wkt;
    wkt << "LINESTRING(";
    auto geom_arr = elem["geometry"];
    if (geom_arr.size() < 2) return;
    
    for (size_t i = 0; i < geom_arr.size(); ++i) {
        wkt << geom_arr[i]["lon"].get<double>() << " " << geom_arr[i]["lat"].get<double>();
        if (i < geom_arr.size() - 1) wkt << ",";
    }
    wkt << ")";
    std::string wkt_str = wkt.str();

    // 3. Routes & Chokepoints
    if (has_tag_value(tags, "highway", "motorway") || has_tag_value(tags, "highway", "trunk")) {
        db.execute_route(id, name, "road", wkt_str, "{}");
    }
    else if (get_tag(tags, "railway") == "rail") {
        db.execute_route(id, name, "rail", wkt_str, "{}");
    }
    else if (get_tag(tags, "power") == "line") {
        db.execute_route(id, "Power Line", "power", wkt_str, "{}");
    }
    else if (get_tag(tags, "man_made") == "pipeline") {
        json state; state["substance"] = get_tag(tags, "substance");
        db.execute_route(id, "Pipeline", "pipeline", wkt_str, state.dump());
    }
    else if (get_tag(tags, "bridge") == "yes") {
        db.execute_choke(name, "bridge", wkt_str, 0.0f);
    }
}

// --- CYCLE EXECUTION ---

void run_ingest_cycle() {
    DatabaseHandler db;
    
    int lat_step = 30;
    int lon_step = 30;
    int total_sectors = (180 / lat_step) * (360 / lon_step);

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "[OSM INGEST] Commencing Global Grid Extraction (" << total_sectors << " Micro-Sectors)" << std::endl;
    std::cout << "=======================================================\n" << std::endl;

    int sector = 1;
    for (int lat = -90; lat < 90; lat += lat_step) {
        for (int lon = -180; lon < 180; lon += lon_step) {
            
            std::string bbox = std::to_string(lat) + "," + std::to_string(lon) + "," + 
                               std::to_string(lat + lat_step) + "," + std::to_string(lon + lon_step);
                               
            std::cout << "\n[SECTOR " << sector << "/" << total_sectors << "] BBOX: [" << bbox << "]" << std::endl;

            auto fetch_with_retries = [&](const std::string& query, const std::string& phase) -> std::string {
                int max_retries = 3;
                for (int attempt = 1; attempt <= max_retries; ++attempt) {
                    std::string result = fetch_overpass_data(query, phase);
                    
                    if (!result.empty() && result[0] == '{') return result;
                    
                    std::cerr << "   -> [" << phase << "] Attempt " << attempt << " failed. ";
                    if (attempt < max_retries) {
                        int backoff = 10 * attempt; 
                        std::cerr << "Retrying in " << backoff << "s..." << std::endl;
                        std::this_thread::sleep_for(std::chrono::seconds(backoff));
                    }
                }
                std::cerr << "   -> [" << phase << "] Skipping sector after 3 failed attempts." << std::endl;
                return "";
            };

            // --- PHASE 1: ASSETS & HUBS ---
            std::string query_points = "[out:json][timeout:180][bbox:" + bbox + "];\n"
                "(\n"
                "  nwr[\"landuse\"=\"quarry\"];\n"
                "  nwr[\"industrial\"~\"^(mine|mining|refinery|oil|chemical|smelter|aluminium_smelting|port|logistics|factory)$\"];\n"
                "  nwr[\"man_made\"=\"offshore_platform\"];\n"
                "  nwr[\"man_made\"=\"storage_tank\"];\n"
                "  nwr[\"product\"~\"metal\"];\n"
                "  nwr[\"power\"~\"^(plant|substation)$\"];\n"
                "  nwr[\"harbour\"=\"yes\"];\n"
                "  nwr[\"railway\"=\"yard\"];\n"
                "  nwr[\"building\"=\"warehouse\"];\n"
                "  nwr[\"aeroway\"=\"aerodrome\"];\n"
                ");\n"
                "out center;";
            
            std::string resp_pts = fetch_with_retries(query_points, "P1:Assets");
            if (!resp_pts.empty()) {
                try {
                    auto j = json::parse(resp_pts);
                    if (j.contains("elements")) {
                        auto elements = j["elements"];
                        for (const auto& elem : elements) process_element_assets_hubs(elem, db);
                        db.force_commit();
                        std::cout << "   -> Indexed " << elements.size() << " Assets/Hubs." << std::endl;
                    }
                } catch (...) {}
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));

            // --- PHASE 2 DECOUPLED: ROUTES & CHOKEPOINTS ---
            // Breaking up the massive line request to prevent memory limits on the Overpass API side
            
            std::vector<std::pair<std::string, std::string>> line_queries = {
                {"P2:Roads_Bridges", "[out:json][timeout:180][bbox:" + bbox + "];\n(\nway[\"highway\"~\"^(motorway|trunk)$\"];\nway[\"bridge\"=\"yes\"];\n);\nout geom;"},
                {"P2:Railways", "[out:json][timeout:180][bbox:" + bbox + "];\n(way[\"railway\"=\"rail\"];);\nout geom;"},
                {"P2:PowerGrid", "[out:json][timeout:180][bbox:" + bbox + "];\n(way[\"power\"=\"line\"];);\nout geom;"},
                {"P2:Pipelines", "[out:json][timeout:180][bbox:" + bbox + "];\n(way[\"man_made\"=\"pipeline\"];);\nout geom;"}
            };

            for (const auto& lq : line_queries) {
                std::string resp_lines = fetch_with_retries(lq.second, lq.first);
                if (!resp_lines.empty()) {
                    try {
                        auto j = json::parse(resp_lines);
                        if (j.contains("elements")) {
                            auto elements = j["elements"];
                            for (const auto& elem : elements) process_element_routes_chokes(elem, db);
                            db.force_commit();
                            std::cout << "   -> Indexed " << elements.size() << " " << lq.first << "." << std::endl;
                        }
                    } catch (...) {}
                }
                // Sleep between sub-phases to cool down connection
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }

            sector++;
        }
    }

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "GLOBAL INGEST SUMMARY" << std::endl;
    std::cout << "Successfully Saved: " << stats_saved << " items." << std::endl;
    std::cout << "Database Skips/Errors: " << stats_db_errors << std::endl;
    std::cout << "=======================================================\n" << std::endl;
    
    stats_saved = 0;
    stats_db_errors = 0;
}

int main() {
    std::cout << "[OSM INGEST] Service Online. Booting Overpass Ingestor." << std::endl;
    
    std::this_thread::sleep_for(std::chrono::seconds(20));

    while (true) {
        try {
            run_ingest_cycle();
        } catch (const std::exception& e) {
            std::cerr << "[OSM INGEST][FATAL] Ingest Cycle Crashed: " << e.what() << std::endl;
        }
        
        std::cout << "[OSM INGEST] Cycle Complete. Entering standby for 1 week (168 Hours)..." << std::endl;
        std::this_thread::sleep_for(std::chrono::hours(168));
    }
    return 0;
}