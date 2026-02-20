#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <hiredis/hiredis.h>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <regex>

using json = nlohmann::json;

// --- CONFIGURATION ---
const std::string DB_CONN = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";
const std::string REDIS_HOST = "corpus_callosum";
const int REDIS_PORT = 6379;

// SEC RSS Feed for all filings (Real-time)
// We filter for 10-Q (Quarterly), 10-K (Annual), 8-K (Significant Events)
const std::string SEC_RSS_URL = "https://www.sec.gov/cgi-bin/browse-edgar?action=getcurrent&type=&company=&dateb=&owner=include&start=0&count=100&output=atom";

struct Filing {
    std::string title;
    std::string link;
    std::string summary;
    std::string updated;
    std::string accession_number; // Unique ID
};

// --- HELPERS ---

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append((char*)contents, total_size);
    return total_size;
}

std::string fetch_rss_feed() {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string buffer;
    struct curl_slist* headers = NULL;
    // SEC requires a proper User-Agent (Company Name + Email)
    headers = curl_slist_append(headers, "User-Agent: RoccoCapital admin@roccocapital.com");

    curl_easy_setopt(curl, CURLOPT_URL, SEC_RSS_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "[FILINGS] CURL Error: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    return buffer;
}

// Basic XML/Atom parsing using Regex (Avoids adding heavy XML libs)
std::vector<Filing> parse_atom_feed(const std::string& xml) {
    std::vector<Filing> filings;
    
    // Regex to find <entry> blocks
    std::regex entry_regex("<entry>([\\s\\S]*?)</entry>");
    auto entries_begin = std::sregex_iterator(xml.begin(), xml.end(), entry_regex);
    auto entries_end = std::sregex_iterator();

    for (std::regex_iterator i = entries_begin; i != entries_end; ++i) {
        std::string block = i->str();
        Filing f;

        // Extract Title (Contains Company + Form Type)
        std::smatch m;
        if (std::regex_search(block, m, std::regex("<title>([\\s\\S]*?)</title>"))) f.title = m[1];
        
        // Extract Link
        if (std::regex_search(block, m, std::regex("<link href=\"(.*?)\""))) f.link = m[1];
        
        // Extract Accession Number (from ID or Link)
        // Link format: .../data/320193/000032019321000010/0000320193-21-000010-index.htm
        if (std::regex_search(f.link, m, std::regex("/([0-9-]{10,})/"))) f.accession_number = m[1];

        // Filter: We only care about 10-Q, 10-K, 8-K
        if (f.title.find("10-Q") != std::string::npos || 
            f.title.find("10-K") != std::string::npos || 
            f.title.find("8-K") != std::string::npos) {
            filings.push_back(f);
        }
    }
    return filings;
}

// Load our active tickers from DB to filter the noise
std::set<std::string> load_watchlist() {
    std::set<std::string> watchlist;
    try {
        pqxx::connection C(DB_CONN);
        pqxx::work W(C);
        pqxx::result R = W.exec("SELECT symbol, metadata->>'company' as name FROM ticker_registry WHERE active = TRUE");
        for (auto row : R) {
            // FIXED: Check for NULLs before converting
            if (!row["symbol"].is_null()) {
                watchlist.insert(row["symbol"].as<std::string>());
            }
            
            if (!row["name"].is_null()) {
                std::string name = row["name"].as<std::string>();
                // Basic normalization (upper case)
                std::transform(name.begin(), name.end(), name.begin(), ::toupper);
                watchlist.insert(name);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[FILINGS] DB Error: " << e.what() << std::endl;
    }
    return watchlist;
}

int main() {
    std::cout << "[FILINGS] SEC Watchdog Online." << std::endl;
    
    redisContext* redis = redisConnect(REDIS_HOST.c_str(), REDIS_PORT);
    if (!redis || redis->err) {
        std::cerr << "[FILINGS] Redis Connection Failed." << std::endl;
        return 1;
    }

    std::set<std::string> processed_cache;
    
    while (true) {
        // 1. Refresh Watchlist (Dynamic)
        std::set<std::string> watchlist = load_watchlist();
        
        // 2. Fetch Feed
        std::string xml = fetch_rss_feed();
        if (!xml.empty()) {
            std::vector<Filing> filings = parse_atom_feed(xml);
            
            for (const auto& f : filings) {
                // Deduplicate
                if (processed_cache.count(f.accession_number)) continue;
                
                // 3. Match against Watchlist
                // Convert title to upper for comparison
                std::string title_upper = f.title;
                std::transform(title_upper.begin(), title_upper.end(), title_upper.begin(), ::toupper);
                
                bool match = false;
                std::string matched_entity = "UNKNOWN";

                for (const auto& target : watchlist) {
                    // Check if Target (Symbol or Name) is in Title
                    if (title_upper.find(target) != std::string::npos) {
                        match = true;
                        matched_entity = target;
                        break;
                    }
                }

                if (match) {
                    std::cout << "[FILINGS] MATCH: " << f.title << " (" << f.link << ")" << std::endl;
                    
                    // 4. Push to Processing Queue (for Python Parser)
                    json task;
                    task["source"] = "SEC_EDGAR";
                    task["entity"] = matched_entity;
                    task["form_type"] = (f.title.find("10-K") != std::string::npos) ? "10-K" : "10-Q";
                    task["url"] = f.link;
                    task["accession"] = f.accession_number;
                    task["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();

                    std::string payload = task.dump();
                    redisCommand(redis, "LPUSH filing_processing_queue %s", payload.c_str());
                    
                    processed_cache.insert(f.accession_number);
                }
            }
        }

        // Poll every 5 minutes (SEC doesn't update instantly)
        std::this_thread::sleep_for(std::chrono::minutes(5));
        
        // Cleanup cache to prevent memory leak (keep last ~1000)
        if (processed_cache.size() > 1000) processed_cache.clear();
    }

    redisFree(redis);
    return 0;
}