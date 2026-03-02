// VVV FILE: ./common/include/structs/StrategyPacket.hpp VVV
#ifndef STRATEGY_PACKET_HPP
#define STRATEGY_PACKET_HPP

#include <string>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct StrategyPacket {
    // --- 1. IDENTITY ---
    std::string strategy_id;    
    std::string symbol;         
    long long timestamp;        

    // --- 2. STRATEGY INTENT ---
    std::string type;           // "DELTA" (Stock), "FUTURE", "OPTION"
    std::string action;         // "OPEN", "UPDATE", "CLOSE", "FLATTEN"
    std::string side;           // "BUY", "SELL"
    
    // --- 3. THALAMUS INPUTS (The "Why") ---
    double fair_value;          
    double market_price;        
    double confidence_score;    
    double volatility_forecast; 
    double z_score;             // NEW: Required by Brainstem to confirm trade direction

    // --- 4. RISK CONSTRAINTS (The "Limits") ---
    double suggested_risk;      
    double catastrophe_stop;    
    double soft_stop;           
    double target_price;        

    // --- HELPER: RATIO CHECK ---
    double get_rr_ratio() const {
        double risk = std::abs(market_price - soft_stop);
        double reward = std::abs(target_price - market_price);
        if (risk <= 0.0001) return 0.0; // Prevent div by zero
        return reward / risk;
    }

    // --- SERIALIZATION ---
    json to_json() const {
        return json{
            {"id", strategy_id}, {"sym", symbol}, {"ts", timestamp},
            {"type", type}, {"act", action}, {"side", side},
            {"fv", fair_value}, {"mkt", market_price},
            {"conf", confidence_score}, {"vol", volatility_forecast},
            {"z_score", z_score}, // NEW: Injected into JSON
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
        p.z_score = j.value("z_score", 0.0); // NEW: Extracted from JSON
        p.suggested_risk = j.value("risk", 0.0);
        p.catastrophe_stop = j.value("hard", 0.0);
        p.soft_stop = j.value("soft", 0.0);
        p.target_price = j.value("tgt", 0.0);
        return p;
    }
};

#endif // STRATEGY_PACKET_HPP
// ^^^ END FILE: ./common/include/structs/StrategyPacket.hpp ^^^