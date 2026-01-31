#include "OptionTicker.hpp"
#include <algorithm>

OptionTicker::OptionTicker(std::string sym, std::string under, double strk, bool call) 
    : BaseTicker(sym, "option", "CBOE") { // Default derivatives exchange
    
    this->underlying_symbol = under;
    this->strike = strk;
    this->is_call = call;
    
    // Initial State: Greeks neutral
    current_state = {0.0, 0.0, 0.5, 0.0, 0.0, 0LL};
}

void OptionTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    
    if (quote.contains("price")) current_state.price = quote["price"].get<double>();
    
    // VOLATILITY (IV)
    if (quote.contains("iv")) current_state.iv = quote["iv"].get<double>();
    
    // GREEKS
    if (quote.contains("delta")) current_state.delta = quote["delta"].get<double>();
    if (quote.contains("gamma")) current_state.gamma = quote["gamma"].get<double>();
    if (quote.contains("theta")) current_state.theta = quote["theta"].get<double>();
    
    current_state.timestamp = quote.value("timestamp", 0LL);
}

double OptionTicker::get_intrinsic_value(double underlying_price) const {
    if (is_call) {
        return std::max(0.0, underlying_price - strike);
    } else {
        return std::max(0.0, strike - underlying_price);
    }
}

json OptionTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"type", instrument_type},
        {"underlying", underlying_symbol},
        {"strike", strike},
        {"is_call", is_call},
        {"price", current_state.price},
        {"iv", current_state.iv},    
        {"delta", current_state.delta},
        {"gamma", current_state.gamma},
        {"theta", current_state.theta},
        {"timestamp", current_state.timestamp}
    };
}