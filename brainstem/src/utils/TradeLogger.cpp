#include "TradeLogger.hpp"
#include <chrono>
#include <iomanip>

std::ofstream TradeLogger::log_file;
std::mutex TradeLogger::log_mtx;

void TradeLogger::init(const std::string& filename) {
    std::lock_guard<std::mutex> lock(log_mtx);
    log_file.open(filename, std::ios::app);
    // Write header if file is new
    log_file << "timestamp,symbol,z_score,target_price,bid,ask,atr,sim_size,action\n";
}

void TradeLogger::log_simulated_trade(const json& signal, double atr, double size, double bid, double ask) {
    std::lock_guard<std::mutex> lock(log_mtx);
    
    auto now = std::chrono::system_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    log_file << ts << ","
             << signal["symbol"].get<std::string>() << ","
             << signal["z_score"].get<float>() << ","
             << signal["target_price"].get<double>() << ","
             << bid << ","
             << ask << ","
             << atr << ","
             << size << ","
             << (signal["z_score"].get<float>() > 0 ? "BUY" : "SELL") << "\n";
    
    log_file.flush();
}