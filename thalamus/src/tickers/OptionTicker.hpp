#ifndef OPTION_TICKER_HPP
#define OPTION_TICKER_HPP

#include "BaseTicker.hpp"

class OptionTicker : public BaseTicker {
public:
    OptionTicker(std::string sym, std::string under, double strk, bool call);
    
    void process_quote(const json& quote) override;
    json get_json_state() const override;

    double get_intrinsic_value(double underlying_price) const;

private:
    struct OptionState {
        double price;
        double iv;      
        double delta;   
        double gamma;   
        double theta;   
        long long timestamp;
    } current_state;

    std::string underlying_symbol; 
    double strike;
    bool is_call;
};

#endif