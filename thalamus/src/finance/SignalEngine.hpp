#ifndef SIGNAL_ENGINE_HPP
#define SIGNAL_ENGINE_HPP

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class SignalEngine {
public:
    // Triggered whenever a physical state change is propagated
    static void calculate_market_deltas(const std::string& entity_id, float new_state_value);

private:
    // Publishes the trade signal back to Redis for the Executor
    static void publish_signal(const std::string& symbol, float z_score, double model_price);
};

#endif