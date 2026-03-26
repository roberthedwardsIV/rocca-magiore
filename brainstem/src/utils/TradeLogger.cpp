#include "TradeLogger.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>

std::ofstream TradeLogger::log_file;
std::mutex TradeLogger::log_mtx;

void TradeLogger::init(const std::string& filename) {
    std::lock_guard<std::mutex> lock(log_mtx);
    std::ifstream check(filename);
    bool is_empty = check.peek() == std::ifstream::traits_type::eof();
    check.close();

    log_file.open(filename, std::ios::app);
    
    if (is_empty) {
        log_file << "timestamp,symbol,action,size,target_price,expected_return,bid,ask,atr\n";
    }
}

void TradeLogger::log_simulated_trade(const json& signal, double atr, double size, double bid, double ask) {
    std::lock_guard<std::mutex> lock(log_mtx);
    if (!log_file.is_open()) return;
    
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::string action = signal.value("action", "UNKNOWN");
    if (action == "UNKNOWN") {
        action = (signal.value("expected_return", 0.0) > 0) ? "BUY" : "SELL";
    }

    // Pull expected_return, fallback to z_score for legacy signals
    double target_metric = signal.value("expected_return", signal.value("z_score", 0.0));

    log_file << ts << ","
             << signal.value("symbol", "UNKNOWN") << ","
             << action << ","
             << size << ","
             << signal.value("target_price", 0.0) << ","
             << target_metric << ","
             << bid << ","
             << ask << ","
             << atr << "\n";
    log_file.flush();
}