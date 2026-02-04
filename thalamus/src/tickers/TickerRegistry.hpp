#ifndef TICKER_REGISTRY_HPP
#define TICKER_REGISTRY_HPP

#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include "BaseTicker.hpp"

struct MacroData {
    double risk_free_rate;    
    double corporate_spread; 
    double equity_risk_premium; 
    long long last_update;
};

class TickerRegistry {
public:
    static bool initialize_from_db();

    static std::shared_ptr<BaseTicker> get_ticker(const std::string& symbol);
    static void for_each_ticker(std::function<void(std::shared_ptr<BaseTicker>)> func);

    static void update_macro_data(double rf, double spread, double erp, long long ts);
    static MacroData get_macro_data();

private:
    static std::unordered_map<std::string, std::shared_ptr<BaseTicker>> ticker_map;
    static std::mutex ticker_mtx;

    static MacroData current_macro; 
    static std::mutex macro_mtx;
};

#endif