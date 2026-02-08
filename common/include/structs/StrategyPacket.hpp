#ifndef STRATEGY_PACKET_HPP
#define STRATEGY_PACKET_HPP

#include <string>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct StrategyPacket {
    // --- 1. IDENTITY ---
    std::string strategy_id;    // UUID (e.g., "EQ_CHILE_COPPER_001")
    std::string symbol;         // "HG"
    long long timestamp;        // Unix Timestamp of signal generation

    // --- 2. STRATEGY INTENT ---
    std::string type;           // "DELTA" (Directional), "VOLATILITY" (Vega/Straddle)
    std::string action;         // "OPEN", "UPDATE", "CLOSE", "FLATTEN"
    std::string side;           // "BUY", "SELL"
    
    // --- 3. THALAMUS INPUTS (The "Why") ---
    double fair_value;          // Where the model thinks price should be
    double market_price;        // Where price IS right now
    double confidence_score;    // 0.0 to 1.0 (0.9 = High Confidence)
    double volatility_forecast; // Predicted ATR (Crucial for Vol plays)
    
    // --- 4. RISK CONSTRAINTS (The "Limits") ---
    double suggested_risk;      // Max dollars to lose on this specific trade
    double catastrophe_stop;    // Hard Stop Price (The "Oh Sh*t" line)
    double soft_stop;           // Soft Stop Price (Thesis invalidation)
    double target_price;        // Take Profit Price

    // --- HELPER: RATIO CHECK ---
    // Returns Reward:Risk ratio. Risk Manager can reject if < 2.0
    double get_rr_ratio() const {
        double risk = std::abs(market_price - soft_stop);
        double reward = std::abs(target_price - market_price);
        
        // Safety for zero division
        if (risk <= 0.0001) return 0.0; 
        
        return reward / risk;
    }

    // --- SERIALIZATION ---
    json to_json() const {
        return json{
            {"id", strategy_id}, {"sym", symbol}, {"ts", timestamp},
            {"type", type}, {"act", action}, {"side", side},
            {"fv", fair_value}, {"mkt", market_price},
            {"conf", confidence_score}, {"vol", volatility_forecast},
            {"risk", suggested_risk}, {"hard", catastrophe_stop},
            {"soft", soft_stop}, {"tgt", target_price}
        };
    }

    static StrategyPacket from_json(const json& j) {
        StrategyPacket p;
        p.strategy_id = j.value("id", "");
        p.symbol = j.value("sym", "");
        p.timestamp = j.value("ts", 0);
        p.type = j.value("type", "DELTA");
        p.action = j.value("act", "OPEN");
        p.side = j.value("side", "BUY");
        p.fair_value = j.value("fv", 0.0);
        p.market_price = j.value("mkt", 0.0);
        p.confidence_score = j.value("conf", 0.0);
        p.volatility_forecast = j.value("vol", 0.0);
        p.suggested_risk = j.value("risk", 0.0);
        p.catastrophe_stop = j.value("hard", 0.0);
        p.soft_stop = j.value("soft", 0.0);
        p.target_price = j.value("tgt", 0.0);
        return p;
    }
};

#endif // STRATEGY_PACKET_HPP