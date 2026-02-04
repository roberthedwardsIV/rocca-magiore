#include "TickerRegistry.hpp"
#include "DatabaseManager.hpp"
#include "StockTicker.hpp"
#include "FutureTicker.hpp"
#include "OptionTicker.hpp"
#include "CommoditySpotTicker.hpp"
#include <pqxx/pqxx>
#include <iostream>

std::unordered_map<std::string, std::shared_ptr<BaseTicker>> TickerRegistry::ticker_map;
std::mutex TickerRegistry::ticker_mtx;

MacroData TickerRegistry::current_macro = {
    0.045,  // RF rate
    0.015,  // Corporate spread
    0.055,  // Equity Risk Premium
    0
};
std::mutex TickerRegistry::macro_mtx;

bool TickerRegistry::initialize_from_db() {
    const std::string conn_str = "dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432";
    try {
        pqxx::connection C(conn_str);
        pqxx::nontransaction N(C);

        // 1. Load Ticker Metadata
        pqxx::result R = N.exec("SELECT symbol, instrument_type, exchange, multiplier, metadata FROM ticker_registry");
        
        for (auto row : R) {
            std::string sym = row["symbol"].as<std::string>();
            std::string type = row["instrument_type"].as<std::string>();
            std::string exch = row["exchange"].as<std::string>();
            double mult = row["multiplier"].as<double>();
            json meta = json::parse(row["metadata"].as<std::string>());

            std::shared_ptr<BaseTicker> ticker;

            if (type == "stock") {
                ticker = std::make_shared<StockTicker>(sym, meta.value("company", ""), meta.value("sector", ""), CompanyFinancials{0,0,0});
            } else if (type == "future") {
                ticker = std::make_shared<FutureTicker>(sym, exch, mult, meta.value("underlying", ""));
            } else if (type == "option") {
                ticker = std::make_shared<OptionTicker>(sym, meta.value("underlying", ""), meta.value("strike", 0.0), meta.value("is_call", true), 0LL);
            } else if (type == "commodity_spot") {
                ticker = std::make_shared<CommoditySpotTicker>(sym, meta.value("name", ""), meta.value("unit", ""));
            }

            if (ticker) {
                ticker_map[sym] = ticker;
            }
        }

        // 2. Load Sensitivity Vectors (The Physical Linkage)
        // Table: ticker_sensitivity (ticker_symbol, entity_id, weight)
        pqxx::result S = N.exec("SELECT ticker_symbol, entity_id, weight FROM ticker_sensitivity");
        for (auto s_row : S) {
            std::string sym = s_row["ticker_symbol"].as<std::string>();
            if (ticker_map.count(sym)) {
                ticker_map[sym]->sensitivity_vector[s_row["entity_id"].as<std::string>()] = s_row["weight"].as<float>();
            }
        }

        std::cout << "[TICKER REGISTRY] Successfully initialized " << ticker_map.size() << " tickers." << std::endl;
        return true;

    } catch (const std::exception &e) {
        std::cerr << "[TICKER REGISTRY ERROR] " << e.what() << std::endl;
        return false;
    }
}

std::shared_ptr<BaseTicker> TickerRegistry::get_ticker(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(ticker_mtx);
    if (ticker_map.count(symbol)) return ticker_map[symbol];
    return nullptr;
}

void TickerRegistry::for_each_ticker(std::function<void(std::shared_ptr<BaseTicker>)> func) {
    std::lock_guard<std::mutex> lock(ticker_mtx);
    for (auto& [sym, ticker] : ticker_map) {
        func(ticker);
    }
}

void TickerRegistry::update_macro_data(double rf, double spread, double erp, long long ts) {
    std::lock_guard<std::mutex> lock(macro_mtx);
    current_macro.risk_free_rate = rf;
    current_macro.corporate_spread = spread;
    current_macro.equity_risk_premium = erp;
    current_macro.last_update = ts;
    std::cout << "[TICKER REGISTRY] Macro Updated: RF=" << rf << " Spread=" << spread << std::endl;
}

MacroData TickerRegistry::get_macro_data() {
    std::lock_guard<std::mutex> lock(macro_mtx);
    return current_macro;
}