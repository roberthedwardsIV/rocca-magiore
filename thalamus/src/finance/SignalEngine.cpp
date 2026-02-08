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
    // Iterate through all tickers to find those linked to this physical entity
    TickerRegistry::for_each_ticker([&](std::shared_ptr<BaseTicker> ticker) {
        
        // We trigger if the ticker is either:
        // A) Explicitly sensitive (Correlation Vector)
        // B) An Owner of the asset (Fundamental Link)
        bool is_sensitive = ticker->sensitivity_vector.count(entity_id);
        
        // Note: In a real scenario, we'd also check 'owned_asset_ids' here, 
        // but your StockTicker recalculates automatically on market data.
        // For now, we rely on the sensitivity map to trigger the evaluation.
        
        if (is_sensitive) {
            float sensitivity = ticker->sensitivity_vector[entity_id];
            
            // 1. GET CURRENT STATE
            json market_data = ticker->get_json_state();
            double market_price = market_data.value("market_price", 0.0);
            if (market_price == 0.0) market_price = market_data.value("price", 0.0); // Fallback
            
            double atr = market_data.value("market_volatility", 0.0); 
            if (atr == 0.0) atr = market_price * 0.015; // Fallback 1.5% vol if missing

            if (market_price <= 0.0) return;

            // 2. DETERMINE TARGET PRICE (The Fix)
            double target_fair_value = 0.0;
            double existing_fundamental_fv = market_data.value("fair_value", 0.0);

            // OPTION A: FUNDAMENTAL MODEL (Prioritize this!)
            // If the StockTicker has already calculated a Fair Value (e.g. based on Mine NPV), use it.
            if (existing_fundamental_fv > 0.0) {
                target_fair_value = existing_fundamental_fv;
                // std::cout << "[THALAMUS] Using Fundamental Value for " << ticker->symbol << std::endl;
            } 
            // OPTION B: SENSITIVITY MODEL (Fallback/Correlation)
            // If we don't have a deep model, estimate impact via linear sensitivity.
            else {
                double impact_magnitude = (1.0f - physical_health);
                double price_delta_pct = sensitivity * impact_magnitude;
                target_fair_value = market_price * (1.0 + price_delta_pct);
            }

            // 3. CALCULATE GAP & SIGNAL STRENGTH
            double gap = target_fair_value - market_price;
            double z_score = gap / atr;

            // 4. CALCULATE CONFIDENCE
            // We assume extreme physical changes (health near 0 or 1) imply higher confidence
            double confidence = 0.5 + (std::abs(0.5 - physical_health) * 0.8); 
            if (confidence > 0.95) confidence = 0.95;

            // 5. PUBLISH IF SIGNIFICANT
            if (std::abs(z_score) > 1.5) { // Threshold 1.5 sigma
                StrategyPacket packet;
                packet.strategy_id = generate_id();
                packet.symbol = ticker->symbol;
                packet.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                
                packet.type = "DELTA";
                packet.side = (target_fair_value > market_price) ? "BUY" : "SELL";
                packet.action = "OPEN";
                
                packet.market_price = market_price;
                packet.fair_value = target_fair_value;
                packet.volatility_forecast = atr;
                packet.confidence_score = confidence;
                
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
                packet.suggested_risk = 1000.0 * confidence;

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
        
        std::cout << "[THALAMUS] Signal: " << packet.symbol 
                  << " | " << packet.side 
                  << " | FV: " << packet.fair_value 
                  << " | Conf: " << packet.confidence_score << std::endl;
    } else {
        // Silent fail or stderr log
        if (c) redisFree(c);
    }
}