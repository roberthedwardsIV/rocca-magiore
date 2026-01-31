/**
*   1 - Initializes connection to PostgreSQL/PostGIS DB (hippocampus)
*   2 - Initializes financial data/tickers from TickerRegistry
*   3 - Starts background threads for Reaper + Snapshotter and detaches
*   4 - Establishes redis connection via connect_redis() helper
*   5 - Runs main event loop
*       a) Listens to "raw_signals" redis channel
*       b) Routes incoming signal packets to the Dispatcher
 */
#include <iostream>
#include <thread>
#include <chrono>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

#include "DatabaseManager.hpp"
#include "Dispatcher.hpp"
#include "Archiver.hpp"
#include "tickers/TickerRegistry.hpp"

using json = nlohmann::json;

// Helper function: establishes redis connnection with 5 retries as needed before quitting
redisContext* connect_redis(const char* host, int port) {
    redisContext* c = nullptr;
    int retries = 5;
    
    while (retries > 0) {
        c = redisConnect(host, port);
        if (c != nullptr && !c->err) {
            std::cout << "[main.cpp] Connected to Redis at " << host << ":" << port << std::endl;
            return c;
        }
        
        std::cerr << "[main.cpp] Redis connection failed. Retrying in 2s..." << std::endl;
        if (c) redisFree(c);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        retries--;
    }
    return nullptr;
}


int main() {
    if (!init_database()) {
        std::cerr << "[main.cpp](FATAL) Could not connect to Hippocampus, Thalamus shutting down.\n";
        return 1;
    }

    if (!TickerRegistry::initialize_from_db()) {
        std::cerr << "[main.cpp](FATAL) Ticker Registry failed to load. Market data unreachable.\n";
        return 1;
    }

    std::thread reaper_thread(Archiver::run_reaper);
    std::thread snapshot_thread(Archiver::run_snapshotter);
    reaper_thread.detach();
    snapshot_thread.detach();

    redisContext *c = connect_redis("corpus_callosum", 6379);
    if (!c) return 1;

    std::cout << "[main.cpp] Supervisor Online. Signal Routing and Handling Active.\n";

    while (true) {
        redisReply *reply = (redisReply*)redisCommand(c, "BRPOP raw_signals 0");
        
        if (reply != nullptr) {
            if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
                try {
                    json sig = json::parse(reply->element[1]->str);
                    Dispatcher::route_signal(sig);
                } catch (const std::exception& e) {
                    std::cerr << "[main.cpp](JSON ERR) Route failed: " << e.what() << std::endl;
                }
            }
            freeReplyObject(reply);
        } else {
            std::cerr << "[main.cpp] Connection lost. Reconnecting..." << std::endl;
            redisFree(c);
            c = connect_redis("corpus_callosum", 6379);
            if (!c) {
                std::cerr << "[main.cpp](FATAL) Could not reconnect to Redis." << std::endl;
                return 1;
            }
        }
    }

    redisFree(c);
    return 0;
}