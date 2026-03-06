#ifndef SIGNAL_ENGINE_HPP
#define SIGNAL_ENGINE_HPP

#include <string>

class SignalEngine {
public:
    // Entry point upon raw_signal reception
    static void evaluate_physical_shock(int origin_asset_id, const std::string& shock_type, float intensity);
    
private:
    // Submission of trade idea to Brainstem
    static void emit_trade(const std::string& ticker, double expected_move, int lag_minutes, double confidence, const std::string& logic_flag);
};

#endif