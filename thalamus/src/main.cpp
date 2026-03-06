#include <iostream>
#include <thread>
#include <chrono>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

#include "DatabaseManager.hpp"
#include "Dispatcher.hpp"

using json = nlohmann::json;

// Helper function: establishes redis connection with retries
redisContext* connect_redis(const char* host, int port) {
    redisContext* c = nullptr;
    int retries = 5;
    
    while (retries > 0) {
        c = redisConnect(host, port);
        if (c != nullptr && !c->err) {
            std::cout << "[THALAMUS main.cpp] Connected to Redis at " << host << ":" << port << std::endl;
            return c;
        }
        
        std::cerr << "[THALAMUS main.cpp] Redis connection failed. Retrying in 2s..." << std::endl;
        if (c) redisFree(c);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        retries--;
    }
    return nullptr;
}

int main() {
    std::cout << "[THALAMUS main.cpp] Booting Stateless Matrix Engine..." << std::endl;

    if (!init_database()) {
        std::cerr << "[THALAMUS main.cpp] (ERR) Could not connect to Hippocampus. Shutting down.\n";
        return 1;
    }

    redisContext *c = connect_redis("corpus_callosum", 6379);
    if (!c) return 1;

    std::cout << "[THALAMUS main.cpp] Online and awaiting signals.\n";
    
    while (true) {
        // Block until a signal arrives in the 'raw_signals' queue
        redisReply *reply = (redisReply*)redisCommand(c, "BRPOP raw_signals 0");
        
        if (reply != nullptr) {
            if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
                try {
                    json sig = json::parse(reply->element[1]->str);
                    Dispatcher::route_signal(sig);
                } catch (const std::exception& e) {
                    std::cerr << "[THALAMUS main.cpp] (ERR) Route failed: " << e.what() << std::endl;
                }
            }
            freeReplyObject(reply);
        } else {
            std::cerr << "[THALAMUS main.cpp] (ERR) Connection lost. Reconnecting..." << std::endl;
            redisFree(c);
            c = connect_redis("corpus_callosum", 6379);
            if (!c) {
                std::cerr << "[THALAMUS main.cpp] (ERR) Could not reconnect to Redis." << std::endl;
                return 1;
            }
        }
    }

    redisFree(c);
    return 0;
}