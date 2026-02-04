#include "ExecutionEngine.hpp"
#include "utils/ContractResolver.hpp"
#include "utils/TradeLogger.hpp"
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>

// FIX: Initialize m_osSignal correctly
ExecutionEngine::ExecutionEngine() 
    : m_osSignal(2000), 
      client(new EClientSocket(this, &m_osSignal)),
      nextOrderId(0) 
{
}

ExecutionEngine::~ExecutionEngine() {
    if (client->isConnected()) client->eDisconnect();
}

bool ExecutionEngine::connect(const char* host, int port, int clientId) {
    bool res = client->eConnect(host, port, clientId);
    if (res) {
        std::cout << "[BRAINSTEM] Connected to IBKR Gateway." << std::endl;
        // Start EReader thread
        reader = std::make_unique<EReader>(client.get(), &m_osSignal);
        reader->start();
    }
    return res;
}

void ExecutionEngine::process_messages() {
    while (client->isConnected()) {
        m_osSignal.waitForSignal();
        reader->processMsgs();
    }
}

void ExecutionEngine::handle_thalamus_signal(const json& signal) {
    std::lock_guard<std::mutex> lock(engine_mtx);
    std::string symbol = signal.value("symbol", "");
    float z_score = signal.value("z_score", 0.0f);

    if (symbol.empty()) return;

    // Mock market data for testing if cache empty
    if (market_cache.find(symbol) == market_cache.end()) {
        market_cache[symbol] = {100.0, 101.0, 100.5, 0};
    }

    MarketData mkt = market_cache[symbol];
    if (mkt.bid <= 0) mkt.bid = mkt.last - 0.05;
    if (mkt.ask <= 0) mkt.ask = mkt.last + 0.05;

    std::string action;
    if (z_score > 2.0) action = "BUY";
    else if (z_score < -2.0) action = "SELL";
    else return;

    double size = calculate_position_size(mkt.last, 0.02);
    if (size <= 0) size = 1; 

    double limit_price = (action == "BUY") ? mkt.bid : mkt.ask;

    std::cout << "[BRAINSTEM] EXECUTING " << action << " " << size << " " << symbol 
              << " @ " << limit_price << " (Z: " << z_score << ")" << std::endl;
              
    place_order(symbol, action, size, limit_price);
}

void ExecutionEngine::place_order(const std::string& symbol, const std::string& action, double quantity, double limit_price) {
    Contract contract = ContractResolver::resolve(symbol);
    
    Order order;
    order.action = action;
    order.orderType = "LMT";
    order.totalQuantity = Decimal(quantity); // Use Decimal
    order.lmtPrice = limit_price;
    order.tif = "DAY";
    order.transmit = true;

    if (client->isConnected()) {
        client->placeOrder(nextOrderId++, contract, order);
    }

    if (action == "BUY") active_positions[symbol] += quantity;
    else active_positions[symbol] -= quantity;

    // FIX: Match the TradeLogger signature
    json log_sig;
    log_sig["symbol"] = symbol;
    log_sig["z_score"] = 0.0; // Not available here
    log_sig["target_price"] = limit_price;
    
    TradeLogger::log_simulated_trade(log_sig, 0.0, quantity, limit_price, limit_price);
}

double ExecutionEngine::calculate_position_size(double price, double volatility) {
    double risk_amt = 1000.0;
    double stop_dist = 2.0 * volatility * price;
    if (stop_dist == 0) return 1;
    return std::floor(risk_amt / stop_dist);
}

void ExecutionEngine::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
    std::lock_guard<std::mutex> lock(engine_mtx);
    if (tickerid_to_symbol.find(tickerId) == tickerid_to_symbol.end()) return;
    std::string sym = tickerid_to_symbol[tickerId];
    if (field == BID) market_cache[sym].bid = price;
    else if (field == ASK) market_cache[sym].ask = price;
    else if (field == LAST) market_cache[sym].last = price;
}

void ExecutionEngine::nextValidId(OrderId orderId) {
    nextOrderId = orderId;
    std::cout << "[IBKR] Next Valid Order ID: " << nextOrderId << std::endl;
}

// FIX: Updated signature with 4th argument
void ExecutionEngine::error(int id, int errorCode, const std::string& errorMsg, const std::string& advancedOrderRejectJson) {
    std::cerr << "[IBKR ERROR] Id: " << id << " Code: " << errorCode << " Msg: " << errorMsg << std::endl;
}

// FIX: Updated signature with Decimal
void ExecutionEngine::orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining, 
                 double avgFillPrice, int permId, int parentId, double lastFillPrice, 
                 int clientId, const std::string& whyHeld, double mktCapPrice) {
    std::cout << "[ORDER STATUS] Id: " << orderId << " Status: " << status << " Filled: " << filled << std::endl;
}

Contract ExecutionEngine::resolve_contract(const std::string& symbol) {
    return ContractResolver::resolve(symbol);
}