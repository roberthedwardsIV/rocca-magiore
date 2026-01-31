#include "ExecutionEngine.hpp"
#include <hiredis/hiredis.h>
#include <thread>

int main() {
    ExecutionEngine engine;
    if (!engine.connect("ibkr_gateway", 4001, 2)) return 1;

    // Start IBKR message processing in a background thread
    std::thread ib_thread([&]() { engine.process_messages(); });

    // Main thread listens to Thalamus Z-Score signals
    redisContext* sub = redisConnect("corpus_callosum", 6379);
    redisReply* reply;
    redisCommand(sub, "SUBSCRIBE execution_signals");

    while (redisGetReply(sub, (void**)&reply) == REDIS_OK) {
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
            auto sig = json::parse(reply->element[2]->str);
            engine.handle_thalamus_signal(sig);
        }
        freeReplyObject(reply);
    }

    ib_thread.join();
    return 0;
}