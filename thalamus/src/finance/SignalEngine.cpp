// VVV FILE: ./thalamus/src/finance/SignalEngine.cpp VVV
#include "SignalEngine.hpp"
#include "../tickers/TickerRegistry.hpp"
#include "../tickers/StockTicker.hpp" 
#include <hiredis/hiredis.h>
#include <iostream>
#include <cmath>
#include <chrono>
#include <random>
#include <iomanip>

// Helper for UUID generation
std::string SignalEngine::generate_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    std::stringstream ss;
    for (int i = 0; i < 8; i++) ss << std::hex << dis(gen);
    return ss.str();
}

void SignalEngine::calculate_market_deltas(const std::string& entity_id, float physical_health) {
    TickerRegistry::for_each_ticker([&](std::shared_ptr<BaseTicker> ticker) {
        
        bool is_sensitive = ticker->sensitivity_vector.count(entity_id);
        
        if (is_sensitive) {
            float sensitivity = ticker->sensitivity_vector[entity_id];
            
            // 1. GET CURRENT STATE
            json market_data = ticker->get_json_state();
            double market_price = market_data.value("market_price", 0.0);
            if (market_price == 0.0) market_price = market_data.value("price", 0.0); 
            
            if (market_price <= 0.0) return; // Can't trade without a price

            // Get Volatility (ATR)
            double atr = market_data.value("market_volatility", 0.0); 
            if (atr <= 0.0) atr = market_price * 0.015; // Fallback: 1.5% daily volatility

            // 2. DETERMINE TARGET PRICE
            double target_fair_value = market_data.value("fair_value", 0.0);

            // OPTION A: FUNDAMENTAL MODEL 
            // If the Ticker has a mathematically derived Fair Value (e.g. DCF NPV), it uses it.
            // OPTION B: SENSITIVITY MODEL 
            // If no fundamental value exists, estimate shock via linear sensitivity.
            if (target_fair_value <= 0.0) {
                double impact_magnitude = (1.0f - physical_health);
                double price_delta_pct = sensitivity * impact_magnitude;
                target_fair_value = market_price * (1.0 + price_delta_pct);
            }

            // 3. CALCULATE GAP & Z-SCORE
            double gap = target_fair_value - market_price;
            double z_score = gap / atr;

            // 4. PUBLISH IF SIGNIFICANT (Alpha Generation)
            // Threshold: 1.5 Standard Deviations
            if (std::abs(z_score) > 1.5) { 
                StrategyPacket packet;
                packet.strategy_id = generate_id();
                packet.symbol = ticker->symbol;
                packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                
                // Set Instrument Type correctly for the Risk Manager
                std::string inst_type = market_data.value("type", "stock");
                if (inst_type == "option") packet.type = "OPTION";
                else if (inst_type == "future") packet.type = "FUTURE";
                else packet.type = "DELTA";

                // Direction logic
                packet.side = (z_score > 0) ? "BUY" : "SELL";
                packet.action = "OPEN";
                
                packet.market_price = market_price;
                packet.fair_value = target_fair_value;
                packet.volatility_forecast = atr;
                packet.z_score = z_score; 
                
                // Confidence scales dynamically with the Z-score (Cap at 0.95)
                double base_conf = 0.5 + (std::abs(z_score) * 0.1);
                packet.confidence_score = std::min(0.95, base_conf);
                
                // Risk Constraints (Brainstem Guidance)
                double stop_dist = atr * 2.0;
                
                if (packet.side == "BUY") {
                    packet.catastrophe_stop = market_price - (stop_dist * 1.5);
                    packet.soft_stop = market_price - stop_dist;
                    packet.target_price = target_fair_value; 
                } else {
                    packet.catastrophe_stop = market_price + (stop_dist * 1.5);
                    packet.soft_stop = market_price + stop_dist;
                    packet.target_price = target_fair_value;
                }

                // Dynamic Risk Sizing ($1000 base * confidence)
                packet.suggested_risk = 1000.0 * packet.confidence_score;

                publish_signal(packet);
            }
        }
    });
}

void SignalEngine::publish_signal(const StrategyPacket& packet) {
    redisContext *c = redisConnect("corpus_callosum", 6379);
    if (c && !c->err) {
        std::string payload = packet.to_json().dump();
        redisCommand(c, "PUBLISH execution_signals %s", payload.c_str());
        redisFree(c);
        
        std::cout << "\n[ALPHA GENERATED] " << packet.symbol 
                  << " | " << packet.side 
                  << " | Z-Score: " << std::fixed << std::setprecision(2) << packet.z_score
                  << " | Mkt: $" << packet.market_price 
                  << " -> FV: $" << packet.fair_value << std::endl;
    } else {
        if (c) redisFree(c);
    }
}
// ^^^ END FILE: ./thalamus/src/finance/SignalEngine.cpp ^^^