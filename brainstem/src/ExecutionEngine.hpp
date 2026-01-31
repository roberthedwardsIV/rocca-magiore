#ifndef EXECUTION_ENGINE_HPP
#define EXECUTION_ENGINE_HPP

#include "IB/EWrapper.h"
#include "IB/EClientSocket.h"
#include "IB/Contract.h"
#include "IB/Order.h"
#include <mutex>
#include <unordered_map>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct MarketData {
    double bid = 0.0;
    double ask = 0.0;
    double last = 0.0;
    long long timestamp = 0;
};

class ExecutionEngine : public EWrapper {
public:
    ExecutionEngine();
    ~ExecutionEngine();

    // 1. Connection
    bool connect(const char* host, int port, int clientId);
    void process_messages(); // The message loop

    // 2. Signal Ingest (From Redis)
    void handle_thalamus_signal(const json& signal);

    // 3. IBKR EWrapper Callbacks
    void nextValidId(OrderId orderId) override;
    void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override;
    void error(int id, int errorCode, const std::string& errorMsg) override;
    void orderStatus(OrderId orderId, const std::string& status, double filled, double remaining, 
                     double avgFillPrice, int permId, int parentId, double lastFillPrice, 
                     int clientId, const std::string& whyHeld, double mktCapPrice) override;

    // Unused callbacks stubbed out for compilation
    void tickSize(TickerId tickerId, TickType field, int size) override {}
    void tickString(TickerId tickerId, TickType field, const std::string& value) override {}
    void tickGeneric(TickerId tickerId, TickType tickType, double value) override {}
    void tickEFP(TickerId tickerId, TickType tickType, double basisPoints, const std::string& formattedBasisPoints,
                 double totalDividends, int holdDays, const std::string& futureLastTradeDate, double dividendImpact,
                 double dividendsToLastTradeDate) override {}
    // ... (Add other standard empty overrides if linker complains, simplified here)

private:
    std::unique_ptr<EClientSocket> client;
    OrderId nextOrderId;
    std::mutex engine_mtx;

    // State Tracking
    std::unordered_map<std::string, int> symbol_to_tickerid;
    std::unordered_map<int, std::string> tickerid_to_symbol;
    std::unordered_map<std::string, MarketData> market_cache;
    
    // Risk Management: Symbol -> Current Net Position (to prevent doubling down)
    std::unordered_map<std::string, double> active_positions; 

    // Internal Logic
    void place_order(const std::string& symbol, const std::string& action, double quantity, double limit_price);
    double calculate_position_size(double price, double volatility);
    Contract resolve_contract(const std::string& symbol);
};

#endif