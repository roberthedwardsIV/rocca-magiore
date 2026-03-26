#include "SignalEngine.hpp"
#include "DatabaseManager.hpp"
#include <iostream>
#include <hiredis/hiredis.h>
#include <chrono>
#include <nlohmann/json.hpp>
#include <cmath>
#include <sstream>

using json = nlohmann::json;

void SignalEngine::evaluate_physical_shock(int origin_asset_id, const std::string& shock_type, float intensity) {
    
    long long current_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Inline helper to process params, apply time-decay, and emit trades
    auto process_params = [&](const std::vector<MatrixParam>& params, const std::string& logic_flag) {
        for (const auto& p : params) {
            
            // Time-Decay for Signal Confidence (Decays after 14D backtest age, max penalty of 50%)
            double age_seconds = current_time - p.last_backtested_ts;
            double age_days = age_seconds / 86400.0;
            
            double time_penalty = 1.0;
            if (age_days > 14.0) {
                double penalty_factor = std::min(0.5, ((age_days - 14.0) / 335.0) * 0.5);
                time_penalty = 1.0 - penalty_factor;
            }
            
            double adjusted_confidence = p.confidence * time_penalty;

            // Filter weak signals (Conf < 0.55)
            if (adjusted_confidence < 0.55) continue;

            // Calculate expected ticker move (Stock or commodity)
            double expected_move = (p.beta * intensity) / 100.0;
            
            emit_trade(p.ticker, expected_move, p.lag_minutes, adjusted_confidence, logic_flag);
        }
    };

    // --- Direct Asset Hits ---
    auto direct_params = get_matrix_parameters(origin_asset_id, shock_type);
    process_params(direct_params, shock_type + "_DIRECT");

    // --- Contagion Hits ---
    std::string contagion_type = shock_type + "_CONTAGION";
    auto downstream_assets = get_downstream_assets(origin_asset_id);
    
    for (int target_id : downstream_assets) {
        auto contagion_params = get_matrix_parameters(target_id, contagion_type);
        process_params(contagion_params, contagion_type);
    }
}

void SignalEngine::emit_trade(const std::string& ticker, double expected_move, int lag_minutes, double confidence, const std::string& logic_flag) {
    // Expected move must be > 0.5% to justify trading costs/spread
    if (std::abs(expected_move) < 0.005) return;

    // --- ORIGINAL BRAINSTEM PACKET (UNTOUCHED) ---
    json packet;
    packet["strategy_id"] = "MATRIX_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    packet["symbol"] = ticker;
    packet["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    packet["action"] = "OPEN";
    packet["side"] = (expected_move > 0) ? "BUY" : "SELL";
    packet["expected_return"] = expected_move;
    packet["lag_minutes"] = lag_minutes;
    packet["conf"] = confidence;

    // Format the UI Log String
    std::ostringstream log_msg;
    log_msg << "[SignalEngine.cpp] ALPHA FOUND " << logic_flag << " -> " << packet["side"].get<std::string>() << " " << ticker 
            << " | Target Move: " << (expected_move * 100.0) << "% | Conf: " << confidence << " | Lag: " << lag_minutes << " mins";
            
    std::string final_log = log_msg.str();
    std::cout << "\n" << final_log << std::endl;

    redisContext *c = redisConnect("corpus_callosum", 6379);
    if (c && !c->err) {
        // 1. Broadcast to Brainstem (execution_signals)
        std::string payload = packet.dump();
        redisReply* reply1 = (redisReply*)redisCommand(c, "PUBLISH execution_signals %s", payload.c_str());
        if (reply1) freeReplyObject(reply1); // Freeing the reply prevents memory leaks in C++
        
        // 2. Broadcast purely to the UI (system_logs)
        json log_json;
        log_json["container"] = "thalamus";
        log_json["log"] = final_log;
        std::string ui_payload = log_json.dump();
        
        redisReply* reply2 = (redisReply*)redisCommand(c, "PUBLISH system_logs %s", ui_payload.c_str());
        if (reply2) freeReplyObject(reply2);

        redisFree(c);
    } else if (c) {
        redisFree(c);
    }
}