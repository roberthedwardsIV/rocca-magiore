#include "ExecutionEngine.hpp"
#include <hiredis/hiredis.h>
#include <thread>
#include <iostream>
#include <chrono>
#include <cstdlib> // ADDED: For std::getenv

int main() {
    ExecutionEngine engine;
    
    // 1. DYNAMIC ENVIRONMENT VARIABLES
    const char* ib_host_env = std::getenv("IB_HOST");
    std::string ib_host = ib_host_env ? ib_host_env : "127.0.0.1";
    
    const char* ib_port_env = std::getenv("IB_PORT");
    int ib_port = ib_port_env ? std::stoi(ib_port_env) : 4001;

    const char* redis_host_env = std::getenv("REDIS_HOST");
    std::string redis_host = redis_host_env ? redis_host_env : "corpus_callosum";

    std::cout << "[BRAINSTEM] Attempting connection to IBKR Gateway at " << ib_host << ":" << ib_port << "..." << std::endl;

    // 2. CONNECT USING ENV VARS (Client ID: 2)
    while (true) {
        if (engine.connect(ib_host.c_str(), ib_port, 2)) {
            std::cout << "[BRAINSTEM] Connection Established." << std::endl;
            break;
        }
        std::cerr << "[BRAINSTEM] Connection failed. Retrying in 10 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    std::thread ib_thread([&]() { engine.process_messages(); });

    // 3. CONNECT REDIS USING ENV VAR
    redisContext* sub = redisConnect(redis_host.c_str(), 6379);
    if (!sub || sub->err) {
        std::cerr << "[BRAINSTEM] Critical: Cannot connect to Redis at " << redis_host << std::endl;
        return 1;
    }

    redisReply* reply;
    redisCommand(sub, "SUBSCRIBE execution_signals");

    std::cout << "[BRAINSTEM] Online and Listening." << std::endl;

    while (redisGetReply(sub, (void**)&reply) == REDIS_OK) {
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
            try {
                auto sig = json::parse(reply->element[2]->str);
                engine.handle_thalamus_signal(sig);
            } catch (...) {
                std::cerr << "[BRAINSTEM] JSON Parse Error" << std::endl;
            }
        }
        freeReplyObject(reply);
    }

    ib_thread.join();
    return 0;
}