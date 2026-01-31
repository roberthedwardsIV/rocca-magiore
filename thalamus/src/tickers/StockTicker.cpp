#include "StockTicker.hpp"

// Constructor
StockTicker::StockTicker(std::string sym, std::string company, std::string sect) 
    : BaseTicker(sym, "stock", "SMART") { // IBKR Smart Routing default
    
    this->company_name = company;
    this->sector = sect;
    
    // Initial State
    // Volatility defaults to 1.5% (0.015) roughly standard daily move for equities
    // Liquidity defaults to 1.0 (assumed liquid until proven otherwise)
    current_state = {0.0, 1.5, 1.0, 0LL}; 
}

// Quote processing 
void StockTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    
    if (quote.contains("price")) {
        current_state.price = quote["price"].get<double>();
    }

    // VOLATILITY UPDATE
    if (quote.contains("volatility")) {
        current_state.volatility = quote["volatility"].get<double>();
    } else if (quote.contains("atr")) {
        // Simple mapping if feed provides ATR instead of % Vol
        current_state.volatility = quote["atr"].get<double>();
    }

    // LIQUIDITY UPDATE
    if (quote.contains("liq")) {
        current_state.liquidity_score = quote["liq"].get<double>();
    }
    
    current_state.timestamp = quote.value("timestamp", 0LL);
}

json StockTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"type", instrument_type},
        {"company", company_name},
        {"sector", sector},
        {"price", current_state.price},
        {"volatility", current_state.volatility}, 
        {"liquidity", current_state.liquidity_score},
        {"timestamp", current_state.timestamp}
    };
}