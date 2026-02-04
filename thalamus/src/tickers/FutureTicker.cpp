#include "FutureTicker.hpp"
#include "TickerRegistry.hpp" 
#include "CommoditySpotTicker.hpp" 
#include <cmath>
#include <algorithm>
#include <iostream>

// Constructor
FutureTicker::FutureTicker(std::string sym, std::string exch, double mult, std::string spot_sym) 
    : BaseTicker(sym, "future", exch), contract_multiplier(mult), underlying_spot_symbol(spot_sym) {
    
    current_state = {
        0.0, 0.0, 0.0,  // price, open interest, volume
        0.0, 0.0, 0.0,  // fair value, implied carry, time
        0LL, 0LL        // timestamps
    };
}


// Market data updates + fair value calculation
void FutureTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    
    if (quote.contains("price")) {
        current_state.market_price = quote["price"].get<double>();
    }
    if (quote.contains("expiry")) {
        current_state.expiry_ts = quote["expiry"].get<long long>();
    }
    if (quote.contains("oi")) {
        current_state.open_interest = quote["oi"].get<double>();
    }
    if (quote.contains("vol")) {
        current_state.volume = quote["vol"].get<double>();
    }
    
    current_state.timestamp = quote.value("timestamp", 0LL);
    
    recalculate_fair_value();
}


// Fair value calculation (via cost to carry formulas)
void FutureTicker::recalculate_fair_value() {
    // Retrieve live Risk-Free Rate
    MacroData macro = TickerRegistry::get_macro_data();
    double r = macro.risk_free_rate; 

    // Retrieve the CommoditySpotTicker for this future 
    auto base_ticker = TickerRegistry::get_ticker(underlying_spot_symbol);
    if (!base_ticker) return;

    // attempt to cast to CommoditySpotTicker to access specific scarcity fields
    auto spot_ticker = std::dynamic_pointer_cast<CommoditySpotTicker>(base_ticker);
    if (!spot_ticker) return;

    // Pull data from the spot ticker's state (uses spot's fair value not market price)
    json spot_state = spot_ticker->get_json_state();
    double S = spot_state.value("fair_value", 0.0); 
    double flow_health = spot_state.value("supply_chain_health", 1.0);

    if (S <= 0) return;

    // time to expiry
    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    double days = (current_state.expiry_ts - now_ms) / (1000.0 * 60.0 * 60.0 * 24.0);
    // minimum 1 hour to avoid division by zero
    current_state.time_to_expiry = std::max(0.0001, days / 365.0);

    // Storage cost calculation (u): 0.0 - 1.0 
    double base_storage = 0.02; 
    double storage_cost = base_storage / std::max(0.1, flow_health); 

    // Cost of Carry Model: F = S * e^((r + u) * t)
    // Convenience yield 'y' is implicitly handled by the Scarcity Premium in S
    double cost_of_carry = r + storage_cost;
    
    current_state.fair_value = S * std::exp(cost_of_carry * current_state.time_to_expiry);

    // Implied Carry (Arbitrage detection)
    if (current_state.market_price > 0) {
        current_state.implied_carry_cost = std::log(current_state.market_price / S) / current_state.time_to_expiry;
    }
}


// JSON packager
json FutureTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"type", instrument_type},
        {"exchange", exchange},
        {"multiplier", contract_multiplier},
        {"underlying", underlying_spot_symbol},
        {"market_price", current_state.market_price},
        {"open_interest", current_state.open_interest},
        {"volume", current_state.volume},
        {"fair_value", current_state.fair_value},
        {"implied_carry_pct", current_state.implied_carry_cost},
        {"time_to_expiry_yrs", current_state.time_to_expiry},       
        {"macro_rf_rate", TickerRegistry::get_macro_data().risk_free_rate},
        {"timestamp", current_state.timestamp}
    };
}