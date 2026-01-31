#include "ExecutionEngine.hpp"
#include "utils/TradeLogger.hpp" // Keep logging for audit trail
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>

ExecutionEngine::ExecutionEngine() 
    : client(new EClientSocket(this, &OSSignal)), nextOrderId(0) {}

ExecutionEngine::~ExecutionEngine() {
    if (client->isConnected()) client->eDisconnect();
}

bool ExecutionEngine::connect(const char* host, int port, int clientId) {
    bool res = client->eConnect(host, port, clientId);
    if (res) {
        std::cout << "[BRAINSTEM] Connected to IBKR Gateway." << std::endl;
        // Start Reader Thread handled by caller or separate EReader class in production
        // For simplicity here, we assume standard TWS API message loop pattern
    }
    return res;
}

// ---------------------------------------------------------
// SIGNAL HANDLING ( The Trigger )
// ---------------------------------------------------------
void ExecutionEngine::handle_thalamus_signal(const json& signal) {
    std::lock_guard<std::mutex> lock(engine_mtx);

    std::string symbol = signal.value("symbol", "");
    float z_score = signal.value("z_score", 0.0f);
    double target_model_price = signal.value("target_price", 0.0);

    if (symbol.empty()) return;

    // 1. DATA VALIDATION CHECK
    // Do we have live market data for this symbol?
    if (market_cache.find(symbol) == market_cache.end()) {
        std::cerr << "[BRAINSTEM] REJECT: No live market data for " << symbol << std::endl;
        // Optional: Trigger subscription here for next time
        return;
    }

    MarketData mkt = market_cache[symbol];
    if (mkt.bid <= 0 || mkt.ask <= 0) {
        std::cerr << "[BRAINSTEM] REJECT: Illiquid/Dark market for " << symbol << std::endl;
        return;
    }

    // 2. DIRECTION DETERMINATION
    std::string action;
    if (z_score > 2.0) action = "BUY";       // Price is artificially low
    else if (z_score < -2.0) action = "SELL"; // Price is artificially high
    else return; // Ignore weak signals (1.0 < Z < 2.0)

    // 3. DUPLICATE CHECK (Risk Management)
    // Don't buy if we are already long
    if (active_positions[symbol] > 0 && action == "BUY") return;
    if (active_positions[symbol] < 0 && action == "SELL") return;

    // 4. EXECUTION
    // Calculate size based on volatility (Kelly Criterion lite)
    // We roughly estimate 'volatility' from the spread or hardcode for now
    double size = calculate_position_size(mkt.last, 0.02); 
    
    // Set Limit Price: Be passive. Join the Bid if buying.
    double limit_price = (action == "BUY") ? mkt.bid : mkt.ask;

    std::cout << "[BRAINSTEM] EXECUTING " << action << " " << size << " " << symbol 
              << " @ " << limit_price << " (Z: " << z_score << ")" << std::endl;

    place_order(symbol, action, size, limit_price);
}

// ---------------------------------------------------------
// ORDER LOGIC
// ---------------------------------------------------------
void ExecutionEngine::place_order(const std::string& symbol, const std::string& action, double quantity, double limit_price) {
    Contract contract = resolve_contract(symbol); // Needs implementation in ContractResolver
    
    Order order;
    order.action = action;
    order.orderType = "LMT";
    order.totalQuantity = quantity;
    order.lmtPrice = limit_price;
    order.tif = "DAY";
    order.transmit = true; // Set to false to test without sending

    client->placeOrder(nextOrderId++, contract, order);
    
    // Update internal position tracker immediately (Optimistic execution)
    // In production, update this only on orderStatus callback
    if (action == "BUY") active_positions[symbol] += quantity;
    else active_positions[symbol] -= quantity;

    // Log to CSV for audit
    TradeLogger::log_simulated_trade(symbol, 0.0, quantity, limit_price, limit_price, 0.0, quantity);
}

double ExecutionEngine::calculate_position_size(double price, double volatility) {
    // Fixed fractional risk model
    // Account = $100,000. Risk per trade = 1% ($1000).
    // Stop distance ~ 2 * Volatility * Price
    // Size = RiskAmount / StopDistance
    
    double risk_amt = 1000.0;
    double stop_dist = 2.0 * volatility * price;
    if (stop_dist == 0) return 0;
    
    return std::floor(risk_amt / stop_dist);
}

// ---------------------------------------------------------
// IBKR CALLBACKS
// ---------------------------------------------------------
void ExecutionEngine::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
    std::lock_guard<std::mutex> lock(engine_mtx);
    if (tickerid_to_symbol.find(tickerId) == tickerid_to_symbol.end()) return;

    std::string sym = tickerid_to_symbol[tickerId];
    
    if (field == BID) market_cache[sym].bid = price;
    else if (field == ASK) market_cache[sym].ask = price;
    else if (field == LAST) market_cache[sym].last = price;
    
    market_cache[sym].timestamp = std::chrono::system_clock::now().time_since_epoch().count();
}

void ExecutionEngine::nextValidId(OrderId orderId) {
    nextOrderId = orderId;
    std::cout << "[IBKR] Next Valid Order ID: " << nextOrderId << std::endl;
}

void ExecutionEngine::error(int id, int errorCode, const std::string& errorMsg) {
    std::cerr << "[IBKR ERROR] Id: " << id << " Code: " << errorCode << " Msg: " << errorMsg << std::endl;
}

void ExecutionEngine::orderStatus(OrderId orderId, const std::string& status, double filled, double remaining, 
                 double avgFillPrice, int permId, int parentId, double lastFillPrice, 
                 int clientId, const std::string& whyHeld, double mktCapPrice) {
    std::cout << "[ORDER STATUS] Id: " << orderId << " Status: " << status << " Filled: " << filled << std::endl;
}

// Minimal Contract Resolver Stub (Should use TickerRegistry metadata in full implementation)
Contract ExecutionEngine::resolve_contract(const std::string& symbol) {
    Contract c;
    c.symbol = symbol;
    c.secType = "STK"; // Default to stock
    c.exchange = "SMART";
    c.currency = "USD";
    
    if (symbol == "HG" || symbol == "CL") {
        c.secType = "FUT";
        c.exchange = (symbol == "HG") ? "COMEX" : "NYMEX";
        c.lastTradeDateOrContractMonth = "202512"; // Hardcoded for example
    }
    return c;
}