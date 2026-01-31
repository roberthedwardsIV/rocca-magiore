#ifndef TRADE_LOGGER_HPP
#define TRADE_LOGGER_HPP

#include <string>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class TradeLogger {
public:
    static void init(const std::string& filename);
    static void log_simulated_trade(const json& signal, double atr, double size, double current_bid, double current_ask);

private:
    static std::ofstream log_file;
    static std::mutex log_mtx;
};

#endif