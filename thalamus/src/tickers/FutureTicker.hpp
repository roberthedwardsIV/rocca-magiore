#ifndef FUTURE_TICKER_HPP
#define FUTURE_TICKER_HPP

#include "BaseTicker.hpp"

struct FutureState {
    double market_price;     // exchange price
    double open_interest;
    double volume;
    
    double fair_value;       // calculated via cost of carry
    double implied_carry_cost; // annualized cost
    double time_to_expiry;     // Years until expiration

    long long expiry_ts;     
    long long timestamp;     // last update
};

class FutureTicker : public BaseTicker {
public:
    FutureTicker(std::string sym, std::string exch, double mult, std::string spot_sym);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;
    
    void recalculate_fair_value();

private:
    FutureState current_state;
    double contract_multiplier;
    std::string underlying_spot_symbol; 
};

#endif