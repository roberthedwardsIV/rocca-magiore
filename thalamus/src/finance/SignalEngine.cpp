#include "SignalEngine.hpp"
#include "../tickers/TickerRegistry.hpp"
#include <hiredis/hiredis.h>
#include <iostream>
#include <cmath>

void SignalEngine::calculate_market_deltas(const std::string& entity_id, float physical_health) {
    // Iterate through all tickers to find those sensitive to this physical entity
    TickerRegistry::for_each_ticker([&](std::shared_ptr<BaseTicker> ticker) {
        if (ticker->sensitivity_vector.count(entity_id)) {
            float sensitivity = ticker->sensitivity_vector[entity_id];
            json market_data = ticker->get_json_state();
            
            double market_price = market_data.value("price", 0.0);
            double atr = market_data.value("atr", 1.0); // Avoid division by zero
            
            if (market_price <= 0.0) return;

            // THE TRANSFORMATION LAYER
            // Simple Linear Model: If health drops, price should increase based on sensitivity
            // Model Price = Market Price * (1 + (Sensitivity * (1.0 - Physical Health)))
            double price_delta_pct = sensitivity * (1.0f - physical_health);
            double model_price = market_price * (1.0 + price_delta_pct);

            // SIGNAL_STRENGTH (Z-Score) = (Model_Price - Market_Price) / ATR
            float z_score = (model_price - market_price) / atr;

            // Only publish if the signal crosses the threshold (e.g., |Z| > 1.0)
            if (std::abs(z_score) > 1.0f) {
                publish_signal(ticker->symbol, z_score, model_price);
            }
        }
    });
}

void SignalEngine::publish_signal(const std::string& symbol, float z_score, double model_price) {
    redisContext *c = redisConnect("corpus_callosum", 6379);
    if (c && !c->err) {
        json signal;
        signal["symbol"] = symbol;
        signal["z_score"] = z_score;
        signal["target_price"] = model_price;
        signal["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string payload = signal.dump();
        redisCommand(c, "PUBLISH execution_signals %s", payload.c_str());
        redisFree(c);
        
        std::cout << "[SIGNAL ENGINE] Opportunity detected in " << symbol << " | Z-Score: " << z_score << std::endl;
    }
}