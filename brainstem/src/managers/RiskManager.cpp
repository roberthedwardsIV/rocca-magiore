#include "RiskManager.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>

RiskManager::RiskManager() : account_balance(100000.0), daily_pnl(0.0) {}

void RiskManager::update_account_state(double balance, double pnl) {
    std::lock_guard<std::mutex> lock(risk_mtx);
    if (balance > 0.0) {
        account_balance = balance;
        daily_pnl = pnl;
    }
}

ApprovalStatus RiskManager::validate_kelly_size(const std::string& sector, double requested_qty, double current_price) { 
    std::lock_guard<std::mutex> lock(risk_mtx);
    ApprovalStatus status = {true, "OK", requested_qty};
    
    double requested_value = requested_qty * current_price;

    // --- GATE 1: THE KILL SWITCH (Daily Drawdown) ---
    double loss_limit = account_balance * MAX_DAILY_LOSS_PCT;
    if (daily_pnl < -loss_limit) {
        return {false, "DAILY_LOSS_LIMIT_EXCEEDED", 0.0};
    }

    // --- GATE 2: SECTOR EXPOSURE (Covariance Fallback) ---
    double current_sector_exposure = sector_exposure[sector];
    double max_sector_value = account_balance * MAX_SECTOR_PCT;
    
    if (current_sector_exposure >= max_sector_value) {
        return {false, "SECTOR_EXPOSURE_MAXED", 0.0};
    }

    // If Kelly requests $30k but sector limit only has $15k room, cap the size instead of rejecting
    if ((current_sector_exposure + requested_value) > max_sector_value) {
        double available_capital = max_sector_value - current_sector_exposure;
        status.adjusted_size = std::floor(available_capital / current_price);
        status.reason = "CAPPED_BY_SECTOR_LIMIT";
    }

    if (status.adjusted_size < 1.0) {
        return {false, "SIZE_TOO_SMALL_AFTER_CAP", 0.0};
    }

    return status;
}

void RiskManager::record_execution(const std::string& sector, double filled_value) {
    std::lock_guard<std::mutex> lock(risk_mtx);
    sector_exposure[sector] += filled_value;
}