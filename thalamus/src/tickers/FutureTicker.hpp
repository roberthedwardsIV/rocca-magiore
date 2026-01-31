#ifndef FUTURE_TICKER_HPP
#define FUTURE_TICKER_HPP

#include "BaseTicker.hpp"

class FutureTicker : public BaseTicker {
public:
    FutureTicker(std::string sym, std::string exch, double mult);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;

private:
    struct FutureState {
        double price;
        double volume;
        double open_interest;    
        long long expiry_ts;     
        long long timestamp;     
    } current_state;

    double contract_multiplier;  
};

#endif