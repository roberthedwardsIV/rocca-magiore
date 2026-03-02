// VVV FILE: ./brainstem/src/managers/RiskManager.hpp VVV
#ifndef RISK_MANAGER_HPP
#define RISK_MANAGER_HPP

#include "structs/StrategyPacket.hpp" 
#include <string>
#include <unordered_map>
#include <mutex>

struct ApprovalStatus {
    bool approved;
    std::string reason;
    double adjusted_size;
};

class RiskManager {
private:
    std::mutex risk_mtx;

    // --- ACCOUNT STATE ---
    double account_balance;
    double daily_pnl;
    
    // --- EXPOSURE TRACKING ---
    std::unordered_map<std::string, double> sector_exposure; 

    // --- HARD LIMITS ---
    const double MAX_DAILY_LOSS_PCT = 0.02;   // 2% Max Daily Drawdown
    const double MAX_SECTOR_PCT = 0.20;       // 20% Max per sector (e.g., METALS)
    const double MAX_SINGLE_TRADE_PCT = 0.05; // 5% Max account risk per trade

public:
    RiskManager();

    void update_account_state(double balance, double pnl);

    // THE FIX: Added sector parameter for accurate exposure tracking
    ApprovalStatus approve_trade(const StrategyPacket& packet, const std::string& sector);
    
    void record_execution(const std::string& sector, double filled_value);
};

#endif // RISK_MANAGER_HPP
// ^^^ END FILE: ./brainstem/src/managers/RiskManager.hpp ^^^