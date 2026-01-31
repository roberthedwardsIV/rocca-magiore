#ifndef STOCK_TICKER_HPP
#define STOCK_TICKER_HPP

#include "BaseTicker.hpp"

class StockTicker : public BaseTicker {
public:
    StockTicker(std::string sym, std::string company, std::string sect);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;

private:
    struct StockState {
        double price;
        double volatility;       
        double liquidity_score;  
        long long timestamp;
    } current_state;

    std::string company_name;
    std::string sector;         
};

#endif