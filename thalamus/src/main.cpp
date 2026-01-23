/**
* main.cpp: The main controller for the thalamus that starts the threads for the Archiver,
*           connects and listens to raw_signals from the corpus collosum, and routes all
*           signals to the dispatcher for proper routing
*/
#include <iostream>
#include <thread>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include "DatabaseManager.hpp"
#include "GlobalRegistry.hpp"
#include "Dispatcher.hpp"
#include "Archiver.hpp"

using json = nlohmann::json;

int main() {
    if (!init_database()) {
        std::cerr << "[FATAL] Could not connect to Hippocampus, Thalamus shutting down.\n";
        return 1;
    }

    std::thread reaper_thread(Archiver::run_reaper);
    std::thread snapshot_thread(Archiver::run_snapshotter);
    reaper_thread.detach();
    snapshot_thread.detach();

    redisContext *c = redisConnect("corpus_callosum", 6379);
    if (c == NULL || c->err) {
        std::cerr << "[FATAL] Redis connection error: " << (c ? c->errstr : "null context") << std::endl;
        return 1;
    }

    std::cout << "[THALAMUS] Supervisor Online. Signal Routing and Handling Active.\n";

    while (true) {
        redisReply *reply = (redisReply*)redisCommand(c, "BRPOP raw_signals 0");
        
        if (reply != nullptr && reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
            try {
                json sig = json::parse(reply->element[1]->str);
                
                Dispatcher::route_signal(sig);

            } catch (const std::exception& e) {
                std::cerr << "[JSON ERR] Failed to route signal: " << e.what() << std::endl;
            }
        }
        freeReplyObject(reply);
    }

    redisFree(c);
    return 0;
}