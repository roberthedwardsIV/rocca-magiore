#ifndef STOCK_TICKER_HPP
#define STOCK_TICKER_HPP

#include "BaseTicker.hpp"
#include <vector>

struct CompanyFinancials {
    double shares_outstanding; 
    double net_debt;           // total debt - cash
    double corporate_overhead; // annual unallocated costs
};

struct StockState {
    double market_price;
    double market_volatility; 
    double liquidity_score;

    double fair_value;         // Calculated NAV per share
    double fair_volatility;    // Derived from physical asset risk profile
    double nav_premium;        // (Market Price - Fair Value) / Fair Value

    long long timestamp;
};

class StockTicker : public BaseTicker {
public:
    StockTicker(std::string sym, std::string company, std::string sect, CompanyFinancials fins);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;

    void add_owned_asset(int asset_id);
    
    void recalculate_fair_value();

private:
    StockState current_state;
    CompanyFinancials financials;
    std::string company_name;
    std::string sector;
    
    std::vector<int> owned_asset_ids;
};

#endif