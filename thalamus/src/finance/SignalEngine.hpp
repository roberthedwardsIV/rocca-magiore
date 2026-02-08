#ifndef SIGNAL_ENGINE_HPP
#define SIGNAL_ENGINE_HPP

#include "structs/StrategyPacket.hpp" // <--- The Shared Contract
#include <string>
#include <vector>

class SignalEngine {
public:
    // Triggered whenever a physical state change is propagated
    // (e.g., Asset Health drops -> Calculate impact on connected Tickers)
    static void calculate_market_deltas(const std::string& entity_id, float new_state_value);

private:
    // Publishes the robust StrategyPacket back to Redis for the Brainstem
    static void publish_signal(const StrategyPacket& packet);
    
    // Helper to generate UUIDs for strategy packets
    static std::string generate_id();
};

#endif // SIGNAL_ENGINE_HPP