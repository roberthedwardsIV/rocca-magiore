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
#include <fstream>
#include <deque>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

using json = nlohmann::json;

// --- CONFIGURATION ---
const std::string DB_CONN = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";
const std::string OVERPASS_URL = "https://overpass-api.de/api/interpreter";
const std::string STATE_FILE = "/shared/osm_ingest_state.json";
const int BATCH_SIZE = 250;
const int MAX_QUADTREE_DEPTH = 3; // Allows 30x30 -> 15x15 -> 7.5x7.5 -> 3.75x3.75

// --- GLOBAL STATS ---
std::atomic<int> stats_db_errors{0};
std::atomic<int> stats_saved{0};

// --- STATE MANAGEMENT ---
struct IngestState {
    int last_lat = -90;
    int last_lon = -180;
    bool is_complete = false;
};

IngestState load_state() {
    IngestState state;
    std::ifstream f(STATE_FILE);
    if (f.is_open()) {
        try {
            json j;
            f >> j;
            state.last_lat = j.value("last_lat", -90);
            state.last_lon = j.value("last_lon", -180);
            state.is_complete = j.value("is_complete", false);
        } catch (...) {}
    }
    return state;
}

void save_state(const IngestState& state) {
    std::ofstream f(STATE_FILE);
    if (f.is_open()) {
        json j;
        j["last_lat"] = state.last_lat;
        j["last_lon"] = state.last_lon;
        j["is_complete"] = state.is_complete;
        f << j.dump(4);
    }
}

// --- QUEUE STRUCTURES ---
struct BoundingBox {
    double min_lat;
    double min_lon;
    double max_lat;
    double max_lon;
    int depth; 
    bool is_marker; // True if this is just a placeholder to trigger a state save
    
    std::string to_string() const {
        return std::to_string(min_lat) + "," + std::to_string(min_lon) + "," + 
               std::to_string(max_lat) + "," + std::to_string(max_lon);
    }
};

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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);         
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);   
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
            response_buffer = ""; 
        } else if (http_code >= 500 && http_code <= 599) {
            std::cerr << "   -> [HTTP " << http_code << "] Overpass API Server Error (Overloaded). Suppressing HTML dump." << std::endl;
            response_buffer = ""; 
        } else if (http_code != 200) {
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
        
        try {
            pqxx::work W(m_db);
            W.exec("ALTER TABLE assets ADD COLUMN IF NOT EXISTS metadata JSONB DEFAULT '{}'::jsonb;");
            W.commit();
        } catch (const std::exception& e) {
            std::cerr << "[OSM INGEST] DB Schema warning: " << e.what() << std::endl;
        }

        reset_transaction();
        
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

    json metadata = json::object();
    if (tags.contains("operator")) metadata["operator"] = tags["operator"];
    if (tags.contains("brand")) metadata["brand"] = tags["brand"];
    if (tags.contains("company")) metadata["company"] = tags["company"];
    if (tags.contains("wikidata")) metadata["wikidata"] = tags["wikidata"];
    std::string metadata_str = metadata.dump();

    if (landuse == "quarry" || industrial == "mine" || industrial == "mining") {
        std::string comms = resource.empty() ? "{Unknown}" : "{" + resource + "}";
        db.execute_asset(name, "mine", comms, lon, lat, metadata_str);
    }
    else if (man_made == "offshore_platform") {
        std::string comms = resource.empty() ? "{oil}" : "{" + resource + "}";
        db.execute_asset(name, "mine", comms, lon, lat, metadata_str);
    }
    else if (industrial == "refinery" || industrial == "oil" || industrial == "chemical") {
        db.execute_asset(name, "refinery", "{}", lon, lat, metadata_str);
    }
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

    IngestState state = load_state();

    if (state.is_complete) {
        std::cout << "\n[OSM INGEST] Previous global pass marked complete. Starting fresh from -90, -180." << std::endl;
        state.last_lat = -90;
        state.last_lon = -180;
        state.is_complete = false;
        save_state(state);
    } else if (state.last_lat != -90 || state.last_lon != -180) {
        std::cout << "\n[OSM INGEST] State File Detected. Resuming ingestion from Lat: " 
                  << state.last_lat << ", Lon: " << state.last_lon << std::endl;
    }

    std::deque<BoundingBox> task_queue;
    int total_base_sectors = 0;
    
    for (int lat = state.last_lat; lat < 90; lat += lat_step) {
        int start_lon = (lat == state.last_lat) ? state.last_lon : -180;
        for (int lon = start_lon; lon < 180; lon += lon_step) {
            task_queue.push_back({(double)lat, (double)lon, (double)(lat + lat_step), (double)(lon + lon_step), 0, false});
            total_base_sectors++;
        }
    }

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "[OSM INGEST] Commencing Task Queue (" << total_base_sectors << " Base Sectors Remaining)" << std::endl;
    std::cout << "=======================================================\n" << std::endl;

    // --- MAIN PROCESSING LOOP ---
    while (!task_queue.empty()) {
        BoundingBox current_box = task_queue.front();
        task_queue.pop_front();
        
        // --- THE FIX: STATE MARKER HANDLING ---
        // If this is a marker, it means all the sub-quadrants of a failed Depth 0 box have finished. 
        // We can now safely advance our saved progress.
        if (current_box.is_marker) {
            state.last_lat = current_box.min_lat;
            state.last_lon = current_box.min_lon + lon_step;
            if (state.last_lon >= 180) {
                state.last_lon = -180;
                state.last_lat = current_box.min_lat + lat_step; 
            }
            save_state(state);
            std::cout << "   -> [STATE SAVED] Recovered and completed subdivided base sector." << std::endl;
            continue;
        }

        std::string bbox = current_box.to_string();
        std::cout << "\n[PROCESSING] BBOX: [" << bbox << "] | Depth: " << current_box.depth << " | Queue Size: " << task_queue.size() << std::endl;

        bool fetch_failed = false;

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
        
        std::string resp_pts = fetch_overpass_data(query_points, "P1:Assets");
        if (resp_pts.empty()) {
            fetch_failed = true;
        } else {
            try {
                auto j = json::parse(resp_pts);
                if (j.contains("elements")) {
                    auto elements = j["elements"];
                    for (const auto& elem : elements) process_element_assets_hubs(elem, db);
                    db.force_commit();
                    std::cout << "   -> Indexed " << elements.size() << " Assets/Hubs." << std::endl;
                }
            } catch (...) {}
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }

        // --- PHASE 2 DECOUPLED: ROUTES & CHOKEPOINTS ---
        if (!fetch_failed) {
            std::vector<std::pair<std::string, std::string>> line_queries = {
                {"P2:Roads_Bridges", "[out:json][timeout:180][bbox:" + bbox + "];\n(\nway[\"highway\"~\"^(motorway|trunk)$\"];\nway[\"bridge\"=\"yes\"];\n);\nout geom;"},
                {"P2:Railways", "[out:json][timeout:180][bbox:" + bbox + "];\n(way[\"railway\"=\"rail\"];);\nout geom;"},
                {"P2:PowerGrid", "[out:json][timeout:180][bbox:" + bbox + "];\n(way[\"power\"=\"line\"];);\nout geom;"},
                {"P2:Pipelines", "[out:json][timeout:180][bbox:" + bbox + "];\n(way[\"man_made\"=\"pipeline\"];);\nout geom;"}
            };

            for (const auto& lq : line_queries) {
                std::string resp_lines = fetch_overpass_data(lq.second, lq.first);
                if (resp_lines.empty()) {
                    fetch_failed = true;
                    break;
                }
                try {
                    auto j = json::parse(resp_lines);
                    if (j.contains("elements")) {
                        auto elements = j["elements"];
                        for (const auto& elem : elements) process_element_routes_chokes(elem, db);
                        db.force_commit();
                        std::cout << "   -> Indexed " << elements.size() << " " << lq.first << "." << std::endl;
                    }
                } catch (...) {}
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }

        // --- THE FIX: QUAD-TREE SUBDIVISION LOGIC ---
        if (fetch_failed) {
            if (current_box.depth < MAX_QUADTREE_DEPTH) {
                std::cout << "   -> [OVERLOAD DETECTED] Subdividing sector into 4 quadrants (New Depth: " << current_box.depth + 1 << ")..." << std::endl;
                
                // If this was a base 30x30 sector, push a marker so we know when its children finish
                if (current_box.depth == 0) {
                    task_queue.push_front({current_box.min_lat, current_box.min_lon, current_box.max_lat, current_box.max_lon, 0, true});
                }
                
                double mid_lat = (current_box.min_lat + current_box.max_lat) / 2.0;
                double mid_lon = (current_box.min_lon + current_box.max_lon) / 2.0;
                int n_depth = current_box.depth + 1;
                
                // Push quadrants to FRONT (Depth-First Execution prevents massive queue memory buildup)
                task_queue.push_front({mid_lat, current_box.min_lon, current_box.max_lat, mid_lon, n_depth, false}); // Top-Left
                task_queue.push_front({mid_lat, mid_lon, current_box.max_lat, current_box.max_lon, n_depth, false}); // Top-Right
                task_queue.push_front({current_box.min_lat, current_box.min_lon, mid_lat, mid_lon, n_depth, false}); // Bottom-Left
                task_queue.push_front({current_box.min_lat, mid_lon, mid_lat, current_box.max_lon, n_depth, false}); // Bottom-Right
            } else {
                std::cerr << "   -> [MAX DEPTH REACHED] Cannot subdivide further. Skipping micro-sector to prevent infinite loop." << std::endl;
            }
            continue; // Skip the standard state save below, as this sector either failed entirely or subdivided
        }

        // --- SAVE PROGRESS (Only for Base Sectors) ---
        if (current_box.depth == 0) {
            state.last_lat = current_box.min_lat;
            state.last_lon = current_box.min_lon + lon_step;
            if (state.last_lon >= 180) {
                state.last_lon = -180;
                state.last_lat = current_box.min_lat + lat_step; 
            }
            save_state(state);
        }
    }

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "GLOBAL INGEST SUMMARY" << std::endl;
    std::cout << "Successfully Saved: " << stats_saved << " items." << std::endl;
    std::cout << "Database Skips/Errors: " << stats_db_errors << std::endl;
    std::cout << "=======================================================\n" << std::endl;
    
    // Mark global cycle as complete
    state.is_complete = true;
    save_state(state);

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