#ifndef COMMODITY_SPOT_TICKER_HPP
#define COMMODITY_SPOT_TICKER_HPP

#include "BaseTicker.hpp"

struct SpotState {
    double market_price;    
    double volatility;
    double daily_change;

    double fair_value;       // Scarcity-adjusted Price
    double scarcity_premium; // Fair Value - Market Price
    double flow_health;      // 0.0 to 1.0

    long long timestamp;
};

class CommoditySpotTicker : public BaseTicker {
public:
    CommoditySpotTicker(std::string sym, std::string name, std::string u);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;

    void update_supply_chain_health(float health_score);

private:
    SpotState current_state;
    std::string commodity_name;
    std::string unit; // "tonne" or "bbl" etc.
    
    void recalculate_fair_value();
};

#endif