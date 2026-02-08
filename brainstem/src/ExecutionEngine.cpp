#include "ExecutionEngine.hpp"
#include "utils/ContractResolver.hpp"
#include "utils/TradeLogger.hpp"
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>

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
        // Start EReader thread to handle incoming messages
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

// --------------------------------------------------------------------------
// STRATEGY LOGIC: The Integration Point
// --------------------------------------------------------------------------
void ExecutionEngine::handle_thalamus_signal(const json& signal) {
    std::lock_guard<std::mutex> lock(engine_mtx);
    
    std::string symbol = signal.value("symbol", "");
    if (symbol.empty()) return;

    // 1. Get Live Market Data
    if (market_cache.find(symbol) == market_cache.end()) {
        std::cerr << "[BRAINSTEM] No market data for " << symbol << ". Ignoring signal." << std::endl;
        return;
    }
    MarketData mkt = market_cache[symbol];
    if (mkt.last <= 0) mkt.last = (mkt.bid + mkt.ask) / 2.0; // Fallback

    // 2. Construct Strategy Packet
    StrategyPacket packet;
    packet.symbol = symbol;
    packet.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Extract Action & Direction
    float z_score = signal.value("z_score", 0.0f);
    if (z_score > 2.0) { packet.action = "OPEN"; packet.side = "BUY"; }
    else if (z_score < -2.0) { packet.action = "OPEN"; packet.side = "SELL"; }
    else return; // Ignore noise

    packet.type = "DELTA"; // Defaulting to directional for now
    packet.market_price = (packet.side == "BUY") ? mkt.ask : mkt.bid;
    packet.fair_value = signal.value("fair_value", packet.market_price); 
    packet.volatility_forecast = signal.value("volatility", packet.market_price * 0.015);
    
    // Set Draft Constraints (Defaults if Thalamus is lazy)
    double atr = packet.volatility_forecast;
    double stop_dist = atr * 2.0;
    
    if (packet.side == "BUY") {
        packet.catastrophe_stop = packet.market_price - (stop_dist * 1.5); // Hard stop
        packet.soft_stop = packet.market_price - stop_dist;                // Soft stop
        packet.target_price = packet.market_price + (stop_dist * 2.5);     // Target
    } else {
        packet.catastrophe_stop = packet.market_price + (stop_dist * 1.5);
        packet.soft_stop = packet.market_price + stop_dist;
        packet.target_price = packet.market_price - (stop_dist * 2.5);
    }
    
    packet.suggested_risk = 1000.0; // Default risk willingness per trade

    // ---------------------------------------------------------
    // GATEKEEPER CHECK: The Risk Manager Veto
    // ---------------------------------------------------------
    ApprovalStatus status = risk_manager.approve_trade(packet);

    if (!status.approved) {
        std::cout << "[RISK REJECT] " << symbol << " denied. Reason: " << status.reason << std::endl;
        return; // <--- BLOCKED
    }

    // ---------------------------------------------------------
    // EXECUTION: Proceed with Approved (Adjusted) Size
    // ---------------------------------------------------------
    double approved_qty = status.adjusted_size;
    std::cout << "[RISK APPROVE] " << symbol << " | Size: " << approved_qty 
              << " | Reason: " << status.reason << std::endl;

    place_order(symbol, packet.side, approved_qty, packet.market_price, packet.catastrophe_stop, packet.target_price);
    
    // Log execution for exposure tracking
    risk_manager.record_execution(get_sector(symbol), approved_qty * packet.market_price);
}

// --------------------------------------------------------------------------
// EXECUTION HELPERS
// --------------------------------------------------------------------------
std::vector<Order> ExecutionEngine::bracket_order(int parentId, const std::string& action, double qty, double limit_price, double stop_price, double take_profit) {
    std::vector<Order> bracket;
    
    // 1. Parent Order (The Entry)
    Order parent;
    parent.orderId = parentId;
    parent.action = action;
    parent.orderType = "LMT";
    parent.totalQuantity = Decimal(qty);
    parent.lmtPrice = limit_price;
    parent.transmit = false; // Don't send yet, wait for children

    // 2. Stop Loss (The Safety Net)
    Order stop;
    stop.orderId = parentId + 1;
    stop.parentId = parentId;
    stop.action = (action == "BUY") ? "SELL" : "BUY";
    stop.orderType = "STP";
    stop.auxPrice = stop_price; // Activation price
    stop.totalQuantity = Decimal(qty);
    stop.transmit = false;

    // 3. Take Profit (The Exit)
    Order profit;
    profit.orderId = parentId + 2;
    profit.parentId = parentId;
    profit.action = (action == "BUY") ? "SELL" : "BUY";
    profit.orderType = "LMT";
    profit.lmtPrice = take_profit;
    profit.totalQuantity = Decimal(qty);
    profit.transmit = true; // SEND ALL 3 NOW

    bracket.push_back(parent);
    bracket.push_back(stop);
    bracket.push_back(profit);

    return bracket;
}

void ExecutionEngine::place_order(const std::string& symbol, const std::string& action, double quantity, double limit_price, double stop_price, double take_profit) {
    
    // --- STEP 1: PAPER TRADING / BACKTEST CHECK ---
    if (paper_mode) {
        std::cout << "[PAPER] BRACKET " << action << " " << quantity << " " << symbol 
                  << " @ " << limit_price << " [Stop: " << stop_price << " | Target: " << take_profit << "]" << std::endl;
        
        json log_sig;
        log_sig["symbol"] = symbol;
        log_sig["action"] = action;
        log_sig["target_price"] = limit_price;
        log_sig["stop_loss"] = stop_price;
        log_sig["take_profit"] = take_profit;
        
        TradeLogger::log_simulated_trade(log_sig, 0.0, quantity, limit_price, limit_price);
        
        // Mock Position Update
        if (action == "BUY") active_positions[symbol] += quantity;
        else active_positions[symbol] -= quantity;
        
        return; 
    }

    // --- STEP 2: LIVE EXECUTION (IBKR) ---
    if (client->isConnected()) {
        Contract contract = ContractResolver::resolve(symbol);
        
        // Create the atomic bracket bundle
        std::vector<Order> bracket = bracket_order(nextOrderId, action, quantity, limit_price, stop_price, take_profit);

        std::cout << "[LIVE] Placing BRACKET ID: " << nextOrderId << " for " << symbol << std::endl;

        for (const auto& o : bracket) {
            client->placeOrder(o.orderId, contract, o);
        }
        
        nextOrderId += 3; // Advance ID for Parent + Stop + Target

    } else {
        std::cerr << "[EXECUTION ERROR] Client not connected." << std::endl;
    }
}

// --------------------------------------------------------------------------
// UTILS & CALLBACKS
// --------------------------------------------------------------------------

double ExecutionEngine::calculate_position_size(double price, double volatility) {
    // Deprecated: Now handled by RiskManager, kept for legacy compatibility
    return 1.0; 
}

std::string ExecutionEngine::get_sector(const std::string& symbol) {
    // Simple mapping for now
    if (symbol == "HG" || symbol == "GC" || symbol == "SI") return "METALS";
    if (symbol == "CL" || symbol == "NG") return "ENERGY";
    if (symbol == "ES" || symbol == "NQ") return "INDICES";
    return "GENERAL";
}

Contract ExecutionEngine::resolve_contract(const std::string& symbol) {
    return ContractResolver::resolve(symbol);
}

// --- IBKR OVERRIDES ---

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
    std::cout << "[IBKR] Next Valid Order ID Synced: " << nextOrderId << std::endl;
}

void ExecutionEngine::error(int id, int errorCode, const std::string& errorMsg, const std::string& advancedOrderRejectJson) {
    if (errorCode == 2104 || errorCode == 2106 || errorCode == 2158) return; // Ignore connectivity noise
    std::cerr << "[IBKR ERROR] Id: " << id << " Code: " << errorCode << " Msg: " << errorMsg << std::endl;
}

void ExecutionEngine::orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining, 
                 double avgFillPrice, int permId, int parentId, double lastFillPrice, 
                 int clientId, const std::string& whyHeld, double mktCapPrice) {
    std::cout << "[ORDER STATUS] Id: " << orderId << " | Status: " << status << " | Filled: " << filled << std::endl;
}