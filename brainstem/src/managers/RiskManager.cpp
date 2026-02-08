#include "RiskManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

RiskManager::RiskManager() 
    : account_balance(100000.0), daily_pnl(0.0) { // Default mock balance until connected
}

void RiskManager::update_account_state(double balance, double pnl) {
    std::lock_guard<std::mutex> lock(risk_mtx);
    account_balance = balance;
    daily_pnl = pnl;
}

ApprovalStatus RiskManager::approve_trade(const StrategyPacket& packet) {
    std::lock_guard<std::mutex> lock(risk_mtx);
    
    ApprovalStatus status = {true, "OK", 0.0};

    // --- GATE 1: THE KILL SWITCH (Daily Drawdown) ---
    // If we have lost > 2% of the account today, STOP EVERYTHING.
    double loss_limit = account_balance * MAX_DAILY_LOSS_PCT;
    if (daily_pnl < -loss_limit) {
        return {false, "DAILY_LOSS_LIMIT_EXCEEDED", 0.0};
    }

    // --- GATE 2: REWARD/RISK RATIO ---
    // We do not take trades that risk $1 to make $0.50.
    // Exception: Volatility plays (Straddles) often have undefined targets.
    if (packet.type == "DELTA" && packet.get_rr_ratio() < 1.5) {
        return {false, "POOR_RISK_REWARD_RATIO (< 1.5)", 0.0};
    }

    // --- GATE 3: VOLATILITY ALIGNMENT ---
    // If Thalamus predicts massive volatility (e.g. 5.0) but market is dead (0.5),
    // the options/stops will be mispriced. 
    bool high_vol_warning = false;
    if (packet.volatility_forecast > 0 && packet.volatility_forecast > (packet.market_price * 0.05)) {
        high_vol_warning = true;
    }

    // --- GATE 4: POSITION SIZING (The Math) ---
    // Calculate Risk Per Share: |Entry - Stop|
    double risk_per_share = std::abs(packet.market_price - packet.catastrophe_stop);
    
    // Safety: Prevent division by zero if stop is missing
    if (risk_per_share <= 0.01) {
        // Fallback: Assume 1% risk if no stop provided (dangerous, but prevents crash)
        risk_per_share = packet.market_price * 0.01;
    }

    // How many shares can we buy without risking more than 'suggested_risk'?
    // e.g. Risk $1000 / $2 risk_per_share = 500 shares
    double max_shares_math = packet.suggested_risk / risk_per_share;

    // --- GATE 5: HARD ACCOUNT CAP ---
    // Never put more than 5% of the TOTAL account into a single trade's risk
    double global_max_risk = account_balance * MAX_SINGLE_TRADE_PCT;
    double global_max_shares = global_max_risk / risk_per_share;

    // Take the smaller of the two sizes (Thalamus suggestion vs Global Limit)
    double final_shares = std::min(max_shares_math, global_max_shares);

    // Volatility Penalty: If High Vol warning, cut size in half to survive swings
    if (high_vol_warning) {
        final_shares *= 0.5;
        status.reason = "APPROVED_WITH_VOL_PENALTY";
    }

    // Check Sector Exposure (e.g. "METALS")
    // Note: We need a way to map Symbol -> Sector. For now, we assume "General".
    // Future: Add 'sector' field to StrategyPacket
    double current_sector_exposure = sector_exposure["General"]; 
    double new_trade_value = final_shares * packet.market_price;
    
    if ((current_sector_exposure + new_trade_value) > (account_balance * MAX_SECTOR_PCT)) {
        return {false, "SECTOR_EXPOSURE_LIMIT", 0.0};
    }

    // Ensure we are buying at least 1 share
    if (final_shares < 1.0) {
        return {false, "SIZE_TOO_SMALL", 0.0};
    }

    status.adjusted_size = std::floor(final_shares);
    return status;
}

void RiskManager::record_execution(const std::string& sector, double filled_value) {
    std::lock_guard<std::mutex> lock(risk_mtx);
    sector_exposure[sector] += filled_value;
}