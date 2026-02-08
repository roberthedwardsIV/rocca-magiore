#ifndef RISK_MANAGER_HPP
#define RISK_MANAGER_HPP

#include "structs/StrategyPacket.hpp" // From your common folder
#include <string>
#include <unordered_map>
#include <mutex>

struct ApprovalStatus {
    bool approved;
    std::string reason;
    double adjusted_size; // The final approved share count
};

class RiskManager {
private:
    std::mutex risk_mtx;

    // --- ACCOUNT STATE ---
    double account_balance;
    double daily_pnl;
    
    // --- EXPOSURE TRACKING ---
    // Maps "Sector" (e.g., "METALS") to current invested dollars
    std::unordered_map<std::string, double> sector_exposure; 

    // --- HARD LIMITS (Config Constants) ---
    const double MAX_DAILY_LOSS_PCT = 0.02;   // Stop trading if down 2% today
    const double MAX_SECTOR_PCT = 0.20;       // Max 20% of account in one sector
    const double MAX_SINGLE_TRADE_PCT = 0.05; // Max 5% risk on one trade

public:
    RiskManager();

    // Call this whenever IBKR sends an account update
    void update_account_state(double balance, double pnl);

    // The Core Gatekeeper Function
    ApprovalStatus approve_trade(const StrategyPacket& packet);
    
    // Helper to log exposure after a trade is filled
    void record_execution(const std::string& sector, double filled_value);
};

#endif // RISK_MANAGER_HPP