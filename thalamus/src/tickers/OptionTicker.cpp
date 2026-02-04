#include "OptionTicker.hpp"
#include "TickerRegistry.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT1_2
#define M_SQRT1_2 0.70710678118654752440
#endif

// Constructor
OptionTicker::OptionTicker(std::string sym, std::string under, double strk, bool call, long long expiry) 
    : BaseTicker(sym, "option", "CBOE"), 
      underlying_symbol(under), 
      strike_price(strk), 
      is_call(call) {
    
    current_state.expiry_ts = expiry;
    current_state.market_price = 0.0;
    current_state.implied_volatility = 0.0;
    current_state.market_delta = 0.0;
    current_state.market_gamma = 0.0;
    current_state.market_theta = 0.0;
    
    current_state.fair_value = 0.0;
    current_state.fair_delta = 0.0;
    current_state.fair_gamma = 0.0;
    current_state.fair_theta = 0.0;
    current_state.pricing_error = 0.0;
    
    current_state.time_to_expiry = 0.0;
    current_state.timestamp = 0;
}


// Helper: standard normal cdf
double OptionTicker::normal_cdf(double value) {
   return 0.5 * std::erfc(-value * M_SQRT1_2);
}


// Helper: standard normal pdf
double OptionTicker::normal_pdf(double value) {
    return (1.0 / std::sqrt(2.0 * M_PI)) * std::exp(-0.5 * value * value);
}


// Market data updates + fair value calculation
void OptionTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    
    if (quote.contains("price")) {
        current_state.market_price = quote["price"].get<double>();
    }
    if (quote.contains("iv")) {
        current_state.implied_volatility = quote["iv"].get<double>();
    }
    
    if (quote.contains("delta")) current_state.market_delta = quote["delta"].get<double>();
    if (quote.contains("gamma")) current_state.market_gamma = quote["gamma"].get<double>();
    if (quote.contains("theta")) current_state.market_theta = quote["theta"].get<double>();

    current_state.timestamp = quote.value("timestamp", 0LL);
    
    recalculate_fair_value();
}


// Fair value calculation (via black-scholes formulas)
void OptionTicker::recalculate_fair_value() {
    auto underlying = TickerRegistry::get_ticker(underlying_symbol);
    if (!underlying) return; 
    
    // check for 'fair_value' first, fallback to market
    json under_state = underlying->get_json_state();
    double S = 0.0;
    if (under_state.contains("fair_value") && under_state["fair_value"].get<double>() > 0) {
        S = under_state["fair_value"].get<double>();
    } else {
        S = under_state.value("market_price", 0.0);
    }
    
    if (S <= 0) return;

    // risk free rate from registry
    MacroData macro = TickerRegistry::get_macro_data();
    double r = macro.risk_free_rate; 

    // time to expiry
    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    double days = (current_state.expiry_ts - now_ms) / (1000.0 * 60.0 * 60.0 * 24.0);
    current_state.time_to_expiry = std::max(0.0001, days / 365.0); 
    double t = current_state.time_to_expiry;

    // volatility (sigma)
    double sigma = current_state.implied_volatility;
    if (sigma <= 0) sigma = 0.5; 

    double K = strike_price;

    // black-scholes calculations
    double d1 = (std::log(S / K) + (r + 0.5 * sigma * sigma) * t) / (sigma * std::sqrt(t));
    double d2 = d1 - sigma * std::sqrt(t);

    double nd1 = normal_cdf(d1);
    double nd2 = normal_cdf(d2);
    double npd1 = normal_pdf(d1); 

    if (is_call) {
        // call price: C = S*N(d1) - K*e^(-rt)*N(d2)
        current_state.fair_value = (S * nd1) - (K * std::exp(-r * t) * nd2);
        
        // delta: N(d1)
        current_state.fair_delta = nd1;
        
        // theta
        double term1 = -(S * sigma * npd1) / (2.0 * std::sqrt(t));
        double term2 = r * K * std::exp(-r * t) * nd2;
        current_state.fair_theta = (term1 - term2) / 365.0; 
        
    } else {
        // put price: P = K*e^(-rt)*N(-d2) - S*N(-d1)
        double n_neg_d1 = normal_cdf(-d1);
        double n_neg_d2 = normal_cdf(-d2);
        
        current_state.fair_value = (K * std::exp(-r * t) * n_neg_d2) - (S * n_neg_d1);
        
        // delta: N(d1) - 1
        current_state.fair_delta = nd1 - 1.0;
        
        // theta
        double term1 = -(S * sigma * npd1) / (2.0 * std::sqrt(t));
        double term2 = r * K * std::exp(-r * t) * n_neg_d2;
        current_state.fair_theta = (term1 + term2) / 365.0; 
    }

    // gamma: N'(d1) / (S * sigma * sqrt(t))
    current_state.fair_gamma = npd1 / (S * sigma * std::sqrt(t));

    // pricing error (signal strength)
    if (current_state.market_price > 0) {
        current_state.pricing_error = current_state.market_price - current_state.fair_value;
    }
}


// JSON packager
json OptionTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"type", instrument_type},
        {"underlying", underlying_symbol},
        {"strike", strike_price},
        {"is_call", is_call},
        {"market_price", current_state.market_price},
        {"implied_volatility", current_state.implied_volatility},
        {"fair_value", current_state.fair_value},
        {"pricing_error", current_state.pricing_error},
        {"fair_delta", current_state.fair_delta},
        {"fair_gamma", current_state.fair_gamma},
        {"fair_theta", current_state.fair_theta},
        {"time_to_expiry_yrs", current_state.time_to_expiry},
        {"macro_rf_rate", TickerRegistry::get_macro_data().risk_free_rate},
        {"timestamp", current_state.timestamp}
    };
}