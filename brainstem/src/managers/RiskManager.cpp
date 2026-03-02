// VVV FILE: ./brainstem/src/managers/RiskManager.cpp VVV
#include "RiskManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

RiskManager::RiskManager() 
    : account_balance(100000.0), daily_pnl(0.0) { 
}

void RiskManager::update_account_state(double balance, double pnl) {
    std::lock_guard<std::mutex> lock(risk_mtx);
    // Sanity check: IBKR occasionally sends 0.0 during server resets
    if (balance > 0.0) {
        account_balance = balance;
        daily_pnl = pnl;
    }
}

ApprovalStatus RiskManager::approve_trade(const StrategyPacket& packet, const std::string& sector) { 
    std::lock_guard<std::mutex> lock(risk_mtx);
    
    ApprovalStatus status = {true, "OK", 0.0};

    // --- GATE 1: THE KILL SWITCH (Daily Drawdown) ---
    double loss_limit = account_balance * MAX_DAILY_LOSS_PCT;
    if (daily_pnl < -loss_limit) {
        return {false, "DAILY_LOSS_LIMIT_EXCEEDED", 0.0};
    }

    // --- GATE 2: PERMISSION FILTER (Level 1 Options) ---
    if (packet.type == "OPTION" && packet.action == "OPEN" && packet.side == "SELL") {
        return {false, "INSUFFICIENT_PERMISSIONS (NO_NAKED_CALLS)", 0.0};
    }

    // --- GATE 3: REWARD/RISK RATIO ---
    if (packet.type == "DELTA" && packet.get_rr_ratio() < 1.5) {
        return {false, "POOR_RISK_REWARD_RATIO (< 1.5)", 0.0};
    }

    // --- GATE 4: VOLATILITY ALIGNMENT ---
    bool high_vol_warning = false;
    if (packet.volatility_forecast > 0 && packet.volatility_forecast > (packet.market_price * 0.05)) {
        high_vol_warning = true;
    }

    // --- GATE 5: POSITION SIZING (The Math) ---
    double risk_per_share = std::abs(packet.market_price - packet.catastrophe_stop);
    if (risk_per_share <= 0.01) risk_per_share = packet.market_price * 0.01;

    double max_shares_math = packet.suggested_risk / risk_per_share;
    double global_max_risk = account_balance * MAX_SINGLE_TRADE_PCT;
    double global_max_shares = global_max_risk / risk_per_share;

    double final_shares = std::min(max_shares_math, global_max_shares);

    // Options Capital Constraint (Max 10% of portfolio on one options play)
    if (packet.type == "OPTION") {
        double total_premium = final_shares * packet.market_price * 100;
        if (total_premium > (account_balance * 0.10)) {
            final_shares = (account_balance * 0.10) / (packet.market_price * 100);
        }
    }

    // Volatility Penalty (Halve position size in extreme chop)
    if (high_vol_warning) {
        final_shares *= 0.5;
        status.reason = "APPROVED_WITH_VOL_PENALTY";
    }

    // --- GATE 6: SECTOR EXPOSURE ---
    // THE FIX: Dynamically track sector exposure instead of hardcoding "General"
    double current_sector_exposure = sector_exposure[sector]; 
    double new_trade_value = final_shares * packet.market_price;
    
    if ((current_sector_exposure + new_trade_value) > (account_balance * MAX_SECTOR_PCT)) {
        return {false, "SECTOR_EXPOSURE_LIMIT", 0.0};
    }

    // --- GATE 7: MINIMUM SIZE ---
    if (final_shares < 1.0 || std::isnan(final_shares)) {
        return {false, "SIZE_TOO_SMALL_OR_NAN", 0.0};
    }

    status.adjusted_size = std::floor(final_shares);
    return status;
}

void RiskManager::record_execution(const std::string& sector, double filled_value) {
    std::lock_guard<std::mutex> lock(risk_mtx);
    sector_exposure[sector] += filled_value;
}
// ^^^ END FILE: ./brainstem/src/managers/RiskManager.cpp ^^^