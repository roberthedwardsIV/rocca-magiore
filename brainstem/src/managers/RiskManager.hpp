#ifndef RISK_MANAGER_HPP
#define RISK_MANAGER_HPP

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
    double account_balance;
    double daily_pnl;
    std::unordered_map<std::string, double> sector_exposure;
    
    // THE HARD LIMITS
    const double MAX_DAILY_LOSS_PCT = 0.02; // 2% Max Daily Drawdown
    const double MAX_SECTOR_PCT = 0.20;     // 20% Max per sector 

public:
    RiskManager();
    void update_account_state(double balance, double pnl);
    
    // VALIDATES the Kelly calculation against hard portfolio boundaries
    ApprovalStatus validate_kelly_size(const std::string& sector, double requested_qty, double current_price);
    void record_execution(const std::string& sector, double filled_value);
};

#endif