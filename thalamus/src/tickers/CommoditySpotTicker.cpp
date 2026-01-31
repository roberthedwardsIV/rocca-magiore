#include "CommoditySpotTicker.hpp"

// Constructor
CommoditySpotTicker::CommoditySpotTicker(std::string sym, std::string name, std::string u) 
    : BaseTicker(sym, "commodity_spot", "GLOBAL_SPOT") { 
    
    this->commodity_name = name;
    this->unit = u;
    
    current_state = {0.0, 0.20, 0.0, 0LL};
}


// Adds newly updated data to ticker state as data streams in
void CommoditySpotTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    
    if (quote.contains("price")) {
        current_state.price = quote["price"].get<double>();
    }
    if (quote.contains("volatility")) {
        current_state.volatility = quote["volatility"].get<double>();
    } else if (quote.contains("atr") && current_state.price > 0) {
        current_state.volatility = quote["atr"].get<double>() / current_state.price;
    }
    if (quote.contains("change_pct")) {
        current_state.daily_change = quote["change_pct"].get<double>();
    }
    
    current_state.timestamp = quote.value("timestamp", 0LL);
}


// Packages commodity spot ticker state to json for archival to db
json CommoditySpotTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"type", instrument_type},
        {"commodity", commodity_name},
        {"unit", unit},
        {"price", current_state.price},
        {"daily_change", current_state.daily_change},
        {"timestamp", current_state.timestamp},
        // Helpful for debugging sensitivity
        {"sensitivity_count", sensitivity_vector.size()} 
    };
}