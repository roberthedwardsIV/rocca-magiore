// VVV FILE: ./thalamus/src/tickers/CommoditySpotTicker.cpp VVV
#include "CommoditySpotTicker.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

// Constructor
CommoditySpotTicker::CommoditySpotTicker(std::string sym, std::string name, std::string u) 
    : BaseTicker(sym, "commodity_spot", "GLOBAL"), commodity_name(name), unit(u) {
    
    current_state = {
        0.0, 0.0, 0.0,  // Market Data
        0.0, 0.0, 1.0,  // Fair Value, Premium, Flow Health
        0LL             // Last update time
    };
}

// Updates from frontal_lobe/sensory_receptor signals
void CommoditySpotTicker::update_supply_chain_health(float health_score) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    
    // Restrict health between 0.01 and 1.0 to prevent division by zero
    current_state.flow_health = std::max(0.01f, std::min(1.0f, health_score));
    
    recalculate_fair_value();
}

// Handle Strict Market Data Updates 
void CommoditySpotTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    
    if (quote.contains("price")) {
        current_state.market_price = quote["price"].get<double>();
    }
    if (quote.contains("volatility")) {
        current_state.volatility = quote["volatility"].get<double>();
    }
    if (quote.contains("change_pct")) {
        current_state.daily_change = quote["change_pct"].get<double>();
    }
    
    current_state.timestamp = quote.value("timestamp", 0LL);
    
    recalculate_fair_value();
}

// Scarcity model for valuation calculation
void CommoditySpotTicker::recalculate_fair_value() {
    if (current_state.market_price <= 0.0) return;

    // SCARCITY MODEL: Inverse Square Law
    // Factor = 1 / (Health^2)
    float scarcity_factor = 1.0f / (current_state.flow_health * current_state.flow_health);
    
    // NEW: Hard cap the scarcity shock. Even if the supply chain completely collapses,
    // spot prices rarely gap more than 300% instantaneously before demand destruction kicks in.
    scarcity_factor = std::min(3.0f, scarcity_factor);
    
    current_state.fair_value = current_state.market_price * scarcity_factor;
    current_state.scarcity_premium = current_state.fair_value - current_state.market_price;
}

// JSON packager
json CommoditySpotTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"type", instrument_type},
        {"commodity", commodity_name},
        {"unit", unit},
        {"market_price", current_state.market_price},
        {"daily_change_pct", current_state.daily_change},
        {"market_volatility", current_state.volatility},
        {"fair_value", current_state.fair_value},
        {"scarcity_premium", current_state.scarcity_premium},
        {"supply_chain_health", current_state.flow_health},
        {"timestamp", current_state.timestamp}
    };
}
// ^^^ END FILE: ./thalamus/src/tickers/CommoditySpotTicker.cpp ^^^