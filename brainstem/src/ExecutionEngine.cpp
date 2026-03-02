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
    redis_pub = redisConnect("corpus_callosum", 6379);
    if (redis_pub == NULL || redis_pub->err) {
        std::cerr << "[BRAINSTEM] Redis publisher init failed: " << (redis_pub ? redis_pub->errstr : "Alloc") << std::endl;
    }
}

ExecutionEngine::~ExecutionEngine() {
    if (client->isConnected()) client->eDisconnect();
    if (redis_pub) redisFree(redis_pub);
}

bool ExecutionEngine::connect(const char* host, int port, int clientId) {
    bool res = client->eConnect(host, port, clientId);
    if (res) {
        std::cout << "[BRAINSTEM] Connected to IBKR Gateway. Awaiting Handshake..." << std::endl;
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
    
    if (!is_ready) {
        std::cerr << "[BRAINSTEM] Warning: IBKR Gateway not ready. Dropping signal." << std::endl;
        return;
    }

    std::string symbol = signal.value("symbol", "");
    if (symbol.empty()) return;

    std::string inst_type = signal.value("instrument_type", "STOCK"); 

    if (market_cache.find(symbol) == market_cache.end()) {
        std::cerr << "[BRAINSTEM] No market data for " << symbol << ". Ignoring signal." << std::endl;
        return;
    }
    
    MarketData mkt = market_cache[symbol];
    if (mkt.last <= 0) mkt.last = (mkt.bid + mkt.ask) / 2.0; 

    StrategyPacket packet;
    packet.symbol = symbol;
    packet.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    
    float z_score = signal.value("z_score", 0.0f);
    if (z_score > 2.0) { packet.action = "OPEN"; packet.side = "BUY"; }
    else if (z_score < -2.0) { packet.action = "OPEN"; packet.side = "SELL"; }
    else return; 

    packet.type = (inst_type == "OPTION") ? "OPTION" : "DELTA"; 
    packet.market_price = (packet.side == "BUY") ? mkt.ask : mkt.bid;
    packet.fair_value = signal.value("fair_value", packet.market_price); 
    packet.volatility_forecast = signal.value("volatility", packet.market_price * 0.015);
    
    double atr = packet.volatility_forecast;
    double stop_dist = atr * 2.0;
    
    if (packet.side == "BUY") {
        packet.catastrophe_stop = packet.market_price - (stop_dist * 1.5); 
        packet.soft_stop = packet.market_price - stop_dist;                
        packet.target_price = packet.market_price + (stop_dist * 2.5);     
    } else {
        packet.catastrophe_stop = packet.market_price + (stop_dist * 1.5);
        packet.soft_stop = packet.market_price + stop_dist;
        packet.target_price = packet.market_price - (stop_dist * 2.5);
    }
    
    packet.suggested_risk = 1000.0; 

    if (inst_type == "FUTURE" || inst_type == "future") {
        std::cout << "\n[MISSED ALPHA] Instrument " << symbol << " is a Future. Logging theoretical execution." << std::endl;
        json shadow_sig = packet.to_json();
        shadow_sig["reason"] = "NO_FUTURE_PERMISSIONS";
        TradeLogger::log_simulated_trade(shadow_sig, packet.volatility_forecast, 1.0, mkt.bid, mkt.ask);
        return; 
    }

    // THE FIX: Pass sector into Risk Manager
    std::string sector = get_sector(symbol);
    ApprovalStatus status = risk_manager.approve_trade(packet, sector);

    if (!status.approved) {
        std::cout << "[RISK REJECT] " << symbol << " denied. Reason: " << status.reason << std::endl;
        return; 
    }

    double approved_qty = status.adjusted_size;
    std::cout << "[RISK APPROVE] " << symbol << " | Size: " << approved_qty 
              << " | Reason: " << status.reason << std::endl;

    place_order(symbol, packet.side, approved_qty, packet.market_price, packet.catastrophe_stop, packet.target_price);
    
    // THE FIX: Record exact sector allocation
    risk_manager.record_execution(sector, approved_qty * packet.market_price);
}

std::vector<Order> ExecutionEngine::bracket_order(int parentId, const std::string& action, double qty, double limit_price, double stop_price, double take_profit) {
    std::vector<Order> bracket;
    
    Order parent;
    parent.orderId = parentId;
    parent.action = action;
    parent.orderType = "LMT";
    parent.totalQuantity = Decimal(qty);
    parent.lmtPrice = limit_price;
    parent.transmit = false; 

    Order stop;
    stop.orderId = parentId + 1;
    stop.parentId = parentId;
    stop.action = (action == "BUY") ? "SELL" : "BUY";
    stop.orderType = "STP";
    stop.auxPrice = stop_price; 
    stop.totalQuantity = Decimal(qty);
    stop.tif = "GTC"; // THE FIX: Ensure stops persist across days
    stop.transmit = false;

    Order profit;
    profit.orderId = parentId + 2;
    profit.parentId = parentId;
    profit.action = (action == "BUY") ? "SELL" : "BUY";
    profit.orderType = "LMT";
    profit.lmtPrice = take_profit;
    profit.totalQuantity = Decimal(qty);
    profit.tif = "GTC"; // THE FIX: Ensure targets persist across days
    profit.transmit = true; 

    bracket.push_back(parent);
    bracket.push_back(stop);
    bracket.push_back(profit);

    return bracket;
}

void ExecutionEngine::place_order(const std::string& symbol, const std::string& action, double quantity, double limit_price, double stop_price, double take_profit) {
    
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
        
        if (action == "BUY") active_positions[symbol] += quantity;
        else active_positions[symbol] -= quantity;
        
        return; 
    }

    if (client->isConnected()) {
        Contract contract = ContractResolver::resolve(symbol);
        
        // --- Track order for Missed Alpha / Rejection Handling ---
        json context;
        context["symbol"] = symbol;
        context["action"] = action;
        context["quantity"] = quantity;
        context["target_price"] = limit_price;
        context["stop_loss"] = stop_price;
        context["take_profit"] = take_profit;
        
        {
            std::lock_guard<std::mutex> lock(engine_mtx);
            pending_orders[nextOrderId] = context;
        }

        std::vector<Order> bracket = bracket_order(nextOrderId, action, quantity, limit_price, stop_price, take_profit);

        std::cout << "[LIVE] Placing BRACKET ID: " << nextOrderId << " for " << symbol << std::endl;

        for (const auto& o : bracket) {
            client->placeOrder(o.orderId, contract, o);
        }

        TradeLogger::log_simulated_trade(context, 0.0, quantity, limit_price, limit_price);
        nextOrderId += 3; 

    } else {
        std::cerr << "[EXECUTION ERROR] Client not connected." << std::endl;
    }
}

// --------------------------------------------------------------------------
// UTILS & CALLBACKS
// --------------------------------------------------------------------------

double ExecutionEngine::calculate_position_size(double price, double volatility) {
    return 1.0; 
}

std::string ExecutionEngine::get_sector(const std::string& symbol) {
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
    is_ready = true; 
    std::cout << "[IBKR] Next Valid Order ID Synced: " << nextOrderId << ". ENGINE READY." << std::endl;
    
    client->reqAccountUpdates(true, "");
    std::cout << "[BRAINSTEM] Subscribed to Live Account & Portfolio Updates." << std::endl;
}

void ExecutionEngine::error(int id, int errorCode, const std::string& errorMsg, const std::string& advancedOrderRejectJson) {
    if (errorCode == 502) return;

    std::cerr << "[IBKR ERROR] Id: " << id << " Code: " << errorCode << " Msg: " << errorMsg << std::endl;
    
    if (errorCode == 2104 || errorCode == 2106 || errorCode == 2158) return; 
    std::cerr << "[IBKR ERROR] Id: " << id << " Code: " << errorCode << " Msg: " << errorMsg << std::endl;
    
    // --- MISSED ALPHA CATCHER ---
    if (errorCode == 200 || errorCode == 201 || errorCode == 321 || errorCode == 354 || errorCode == 451 || errorCode == 10167) {
        std::lock_guard<std::mutex> lock(engine_mtx);
        if (pending_orders.find(id) != pending_orders.end()) {
            json log_sig = pending_orders[id];
            log_sig["status"] = "REJECTED_PERMISSIONS";
            log_sig["reject_code"] = errorCode;
            
            std::cout << "\n[MISSED ALPHA] " << log_sig["symbol"].get<std::string>() 
                      << " trade rejected by API. Logging theoretical execution to CSV." << std::endl;
            
            double qty = log_sig.value("quantity", 0.0);
            double price = log_sig.value("target_price", 0.0);
            TradeLogger::log_simulated_trade(log_sig, 0.0, qty, price, price);
            
            pending_orders.erase(id);
        }
    }
}

void ExecutionEngine::orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining, 
                 double avgFillPrice, int permId, int parentId, double lastFillPrice, 
                 int clientId, const std::string& whyHeld, double mktCapPrice) {
    std::cout << "[ORDER STATUS] Id: " << orderId << " | Status: " << status << " | Filled: " << filled << std::endl;

    // Clean up tracking map on success
    if (status == "Filled" || status == "Submitted" || status == "PreSubmitted") {
        std::lock_guard<std::mutex> lock(engine_mtx);
        if (pending_orders.find(orderId) != pending_orders.end()) {
            pending_orders.erase(orderId);
        }
    }
}

void ExecutionEngine::updateAccountValue(const std::string& key, const std::string& val, const std::string& currency, const std::string& accountName) {
    std::lock_guard<std::mutex> lock(engine_mtx);
    bool updated = false;

    try {
        if (key == "NetLiquidation") {
            current_balance = std::stod(val);
            updated = true;
        } else if (key == "UnrealizedPnL") {
            current_pnl = std::stod(val);
            updated = true;
        }
    } catch (...) {}

    if (updated) {
        risk_manager.update_account_state(current_balance, current_pnl);

        if (redis_pub && !redis_pub->err) {
            json j;
            j["type"] = "account_update";
            j["balance"] = current_balance;
            j["pnl"] = current_pnl;
            j["currency"] = currency;
            j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::string payload = j.dump();
            redisCommand(redis_pub, "SET account_state %s", payload.c_str());
            redisCommand(redis_pub, "PUBLISH state_vectors %s", payload.c_str());
        }
    }
}

void ExecutionEngine::updatePortfolio(const Contract& contract, Decimal position, double marketPrice, double marketValue, double averageCost, double unrealizedPNL, double realizedPNL, const std::string& accountName) {
    std::lock_guard<std::mutex> lock(engine_mtx);
    
    double pos_val = decimalToDouble(position);
    active_positions[contract.symbol] = pos_val;

    if (redis_pub && !redis_pub->err) {
        json j;
        j["type"] = "portfolio_update";
        j["symbol"] = contract.symbol;
        j["position"] = pos_val;
        j["market_price"] = marketPrice;
        j["market_value"] = marketValue;
        j["unrealized_pnl"] = unrealizedPNL;
        j["average_cost"] = averageCost;
        
        std::string payload = j.dump();
        redisCommand(redis_pub, "HSET active_portfolio %s %s", contract.symbol.c_str(), payload.c_str());
        redisCommand(redis_pub, "PUBLISH state_vectors %s", payload.c_str());
    }
}