#include "ExecutionEngine.hpp"
#include <hiredis/hiredis.h>
#include <thread>
#include <iostream>
#include <chrono>

int main() {
    ExecutionEngine engine;
    
    std::cout << "[BRAINSTEM] Attempting connection to IBKR Gateway..." << std::endl;

    // ADDED: Infinite Retry Loop
    while (true) {
        // Connect to localhost (Sidecar Network) on Port 4002 (Paper)
        if (engine.connect("127.0.0.1", 4002, 2)) {
            std::cout << "[BRAINSTEM] Connection Established." << std::endl;
            break;
        }
        std::cerr << "[BRAINSTEM] Connection failed. Retrying in 10 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    // Start IBKR message processing in a background thread
    std::thread ib_thread([&]() { engine.process_messages(); });

    // Main thread listens to Thalamus Z-Score signals
    redisContext* sub = redisConnect("corpus_callosum", 6379);
    if (!sub || sub->err) {
        std::cerr << "[BRAINSTEM] Critical: Cannot connect to Redis." << std::endl;
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