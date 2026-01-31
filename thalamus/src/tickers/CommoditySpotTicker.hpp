#ifndef COMMODITY_SPOT_TICKER_HPP
#define COMMODITY_SPOT_TICKER_HPP

#include "BaseTicker.hpp"

class CommoditySpotTicker : public BaseTicker {
public:
    CommoditySpotTicker(std::string sym, std::string name, std::string u);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;

private:
    struct SpotState {
        double price; 
        double volatility;       
        double daily_change; 
        long long timestamp;
    } current_state;

    std::string commodity_name; 
    std::string unit;           
};

#endif