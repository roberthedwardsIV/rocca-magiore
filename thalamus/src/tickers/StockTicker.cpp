// VVV FILE: ./thalamus/src/tickers/StockTicker.cpp VVV
#include "StockTicker.hpp"
#include "../GlobalRegistry.hpp" 
#include "../assets/MineAsset.hpp"
#include "../assets/RefineryAsset.hpp"
#include "../assets/SmelterAsset.hpp" // NEW: Added Smelter Support
#include "TickerRegistry.hpp"  
#include <cmath>
#include <iostream>
#include <numeric>

// Constructor
StockTicker::StockTicker(std::string sym, std::string company, std::string sect, CompanyFinancials fins) 
    : BaseTicker(sym, "stock", "SMART"), financials(fins) {
    
    this->company_name = company;
    this->sector = sect;
    
    current_state = {
        0.0, 1.5, 1.0,  // Price, Vol, Liq
        0.0, 0.20, 0.0, // Val, Vol, Premium
        0LL             // Timestamp
    };
}

// Link a physical asset to this stock
void StockTicker::add_owned_asset(int asset_id) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    owned_asset_ids.push_back(asset_id);
}

// Handle live market data updates + reprice
void StockTicker::process_quote(const json& quote) {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    if (quote.contains("price")) current_state.market_price = quote["price"].get<double>();
    if (quote.contains("volatility")) current_state.market_volatility = quote["volatility"].get<double>();
    
    current_state.timestamp = quote.value("timestamp", 0LL);
    
    recalculate_fair_value(); 
}

// Sum-of-the-Parts (SOTP) Calculation for Fair Value
void StockTicker::recalculate_fair_value() {
    double total_asset_value = 0.0;
    double weighted_risk_sum = 0.0;
    
    for (int id : owned_asset_ids) {
        auto asset = GlobalRegistry::get_asset(id);
        if (!asset) continue;

        // --- SUM OF THE PARTS AGGREGATION ---
        if (asset->entity_type == "mine") {
            auto mine = std::dynamic_pointer_cast<MineAsset>(asset);
            if (mine) {
                json state = mine->get_json_state();
                double npv = state.value("npv", 0.0);
                double threat = state.value("threat_level", 0.0);
                
                total_asset_value += npv;
                weighted_risk_sum += (npv * threat); 
            }
        } 
        else if (asset->entity_type == "refinery") {
            auto ref = std::dynamic_pointer_cast<RefineryAsset>(asset);
            if (ref) {
                json state = ref->get_json_state();
                double ev = state.value("enterprise_value", 0.0);
                double risk = state.value("containment_risk", 0.0);
                
                total_asset_value += ev;
                weighted_risk_sum += (ev * risk);
            }
        }
        // NEW: Smelter Integration
        else if (asset->entity_type == "smelter") {
            auto smelter = std::dynamic_pointer_cast<SmelterAsset>(asset);
            if (smelter) {
                json state = smelter->get_json_state();
                double ev = state.value("enterprise_value", 0.0);
                double risk = state.value("threat_level", 0.0); // Assume standard threat for now
                
                total_asset_value += ev;
                weighted_risk_sum += (ev * risk);
            }
        }
    }

    // --- CORPORATE ADJUSTMENTS ---
    MacroData macro = TickerRegistry::get_macro_data();
    double corporate_discount_rate = std::max(0.01, macro.risk_free_rate + macro.equity_risk_premium); 
    double overhead_multiple = 1.0 / corporate_discount_rate;
    
    double corp_penalty = financials.corporate_overhead * overhead_multiple;
    double enterprise_value = total_asset_value - corp_penalty;

    // Equity Value = Enterprise Value - Net Debt
    double equity_value = enterprise_value - financials.net_debt;

    // --- PER SHARE NAV CALCULATION ---
    // Safety check: Prevent division by zero if SEC pipeline hasn't loaded shares yet
    if (financials.shares_outstanding > 0) {
        current_state.fair_value = std::max(0.0, equity_value / financials.shares_outstanding);
    } else {
        // Fallback: If we don't have share count, we cannot calculate a per-share NAV.
        // We set it to 0 so the SignalEngine ignores it rather than trading on garbage.
        current_state.fair_value = 0.0; 
    }

    // Fair Volatility Calculation (Derived from physical asset threat levels)
    double portfolio_risk_factor = (total_asset_value > 0) ? (weighted_risk_sum / total_asset_value) : 0.0;
    current_state.fair_volatility = 0.20 * (1.0 + portfolio_risk_factor);

    // Calculate Alpha Signal Premium
    if (current_state.fair_value > 0 && current_state.market_price > 0) {
        current_state.nav_premium = (current_state.market_price - current_state.fair_value) / current_state.fair_value;
    }
}

// JSON packager
json StockTicker::get_json_state() const {
    std::lock_guard<std::mutex> lock(ticker_mutex);
    return {
        {"symbol", symbol},
        {"type", "stock"},
        {"company", company_name},
        {"market_price", current_state.market_price},
        {"market_volatility", current_state.market_volatility},
        {"fair_value", current_state.fair_value},
        {"nav_premium", current_state.nav_premium},
        {"fair_volatility", current_state.fair_volatility},
        {"owned_assets_count", owned_asset_ids.size()},
        {"timestamp", current_state.timestamp}
    };
}
// ^^^ END FILE: ./thalamus/src/tickers/StockTicker.cpp ^^^