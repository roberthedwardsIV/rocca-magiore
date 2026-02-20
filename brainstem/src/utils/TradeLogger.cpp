#include "TradeLogger.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>

// Define static members
std::ofstream TradeLogger::log_file;
std::mutex TradeLogger::log_mtx;

void TradeLogger::init(const std::string& filename) {
    std::lock_guard<std::mutex> lock(log_mtx);
    
    // Check if file exists/is empty before opening in append mode
    std::ifstream check(filename);
    bool is_empty = check.peek() == std::ifstream::traits_type::eof();
    check.close();

    log_file.open(filename, std::ios::app);
    
    if (is_empty) {
        log_file << "timestamp,symbol,action,size,target_price,z_score,bid,ask,atr\n";
    }
}

void TradeLogger::log_simulated_trade(const json& signal, double atr, double size, double bid, double ask) {
    std::lock_guard<std::mutex> lock(log_mtx);
    if (!log_file.is_open()) return;
    
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // Logic to determine action string
    std::string action = "UNKNOWN";
    if (signal.contains("action")) {
        action = signal["action"].get<std::string>();
    } else {
        // Fallback for legacy signals
        action = (signal.value("z_score", 0.0f) > 0) ? "BUY" : "SELL";
    }

    log_file << ts << ","
             << signal.value("symbol", "UNKNOWN") << ","
             << action << ","
             << size << ","
             << signal.value("target_price", 0.0) << ","
             << signal.value("z_score", 0.0) << ","
             << bid << ","
             << ask << ","
             << atr << "\n";
             
    log_file.flush();
}