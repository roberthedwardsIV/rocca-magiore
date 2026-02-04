#ifndef OPTION_TICKER_HPP
#define OPTION_TICKER_HPP

#include "BaseTicker.hpp"

struct OptionState {
    // market data
    double market_price;     
    double implied_volatility; 
    double market_delta;     
    double market_gamma;     
    double market_theta;     

    // internal valuation
    double fair_value;       
    double fair_delta;       
    double fair_gamma;       
    double fair_theta;       
    double pricing_error;    

    // time
    double time_to_expiry;   
    long long expiry_ts;     
    long long timestamp;     
};

class OptionTicker : public BaseTicker {
public:
    OptionTicker(std::string sym, std::string under, double strk, bool call, long long expiry);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;
    
    void recalculate_fair_value();

private:
    OptionState current_state;
    std::string underlying_symbol;
    double strike_price;
    bool is_call;
    
    double normal_cdf(double value);
    double normal_pdf(double value);
};

#endif