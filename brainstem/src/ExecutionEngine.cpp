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
    const char* redis_host_env = std::getenv("REDIS_HOST");
    std::string redis_host = redis_host_env ? redis_host_env : "corpus_callosum";
    
    redis_pub = redisConnect(redis_host.c_str(), 6379);
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
// TSDB Queries & Analysis (Blueprint 5)
// --------------------------------------------------------------------------
TSDBMetrics ExecutionEngine::fetch_quant_metrics(const std::string& symbol) {
    TSDBMetrics metrics;
    try {
        pqxx::connection C(tsdb_conn_str);
        pqxx::nontransaction N(C);
        std::string query = "SELECT atr_14, vol_60_annualized, close FROM market_daily_metrics WHERE symbol = " + N.quote(symbol) + " ORDER BY day DESC LIMIT 1";
        pqxx::result R = N.exec(query);
        
        if (!R.empty()) {
            metrics.atr_14 = R[0]["atr_14"].as<double>();
            metrics.vol_60_annualized = R[0]["vol_60_annualized"].as<double>();
            metrics.last_close = R[0]["close"].as<double>();
            metrics.is_valid = true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[TSDB ERR] " << e.what() << std::endl;
    }
    return metrics;
}

double ExecutionEngine::fetch_portfolio_covariance(const std::string& new_symbol) {
    return 0.15; // Baseline covariance factor
}

// NEW: Fetches the latest known live price directly from TimescaleDB
double ExecutionEngine::fetch_latest_price(const std::string& symbol) {
    try {
        // Connect to the market_data_system TSDB
        pqxx::connection C(tsdb_conn_str); 
        pqxx::nontransaction N(C);
        
        // THE FIX: Query market_1m for the live streaming price, NOT historical_daily
        std::string sql = "SELECT close FROM market_1m WHERE symbol = " + N.quote(symbol) + " ORDER BY time DESC LIMIT 1";
        pqxx::result R = N.exec(sql);
        
        if (!R.empty()) {
            return R[0][0].as<double>();
        }
    } catch (const std::exception& e) {
        std::cerr << "[TSDB ERR] Failed to fetch latest price for " << symbol << ": " << e.what() << std::endl;
    }
    return 0.0;
}

// --------------------------------------------------------------------------
// BLUEPRINT 1: Signal Coalescence (Probabilistic Union & Tranching)
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

    signal_buffer[symbol].push_back(signal);
    execute_coalesced_signals(symbol, signal_buffer[symbol], inst_type);
    signal_buffer[symbol].clear();
}

void ExecutionEngine::execute_coalesced_signals(const std::string& symbol, const std::vector<json>& signals, const std::string& inst_type) {
    if (signals.empty()) return;

    double expected_remaining = 1.0;
    double volume_weighted_lag = 0.0;
    double total_weight = 0.0;
    
    double first_val = signals[0].value("expected_return", signals[0].value("z_score", 0.0));
    std::string side = (first_val > 0) ? "BUY" : "SELL"; 
    
    // THE FIX: Call your new TSDB fetcher!
    double execution_price = fetch_latest_price(symbol); 
    
    if (execution_price <= 0) {
        std::cerr << "[BRAINSTEM] No TSDB market data for " << symbol << ". Ignoring signal." << std::endl;
        return;
    }

    for (const auto& sig : signals) {
        // Fallback to z_score scaling if expected_return missing
        double e_r = sig.value("expected_return", sig.value("z_score", 0.0) * 0.01);
        int lag = sig.value("lag_minutes", 0);
        double conf = sig.value("conf", 1.0);
        
        expected_remaining *= (1.0 - std::abs(e_r)); 
        volume_weighted_lag += (lag * conf);
        total_weight += conf;
    }

    double net_expected_return = 1.0 - expected_remaining;
    if (side == "SELL") net_expected_return = -net_expected_return;
    
    int vwal = (total_weight > 0) ? static_cast<int>(volume_weighted_lag / total_weight) : 0;

    TSDBMetrics metrics = fetch_quant_metrics(symbol);
    if (!metrics.is_valid || metrics.vol_60_annualized <= 0) {
        std::cerr << "[QUANT EXEC] TSDB Metrics invalid or missing for " << symbol << std::endl;
        return;
    }

    // --- PRESERVED: MISSED ALPHA CATCHER FOR FUTURES ---
    if (inst_type == "FUTURE" || inst_type == "future") {
        std::cout << "\n[MISSED ALPHA] Instrument " << symbol << " is a Future. Logging theoretical execution." << std::endl;
        json shadow_sig;
        shadow_sig["symbol"] = symbol;
        shadow_sig["action"] = side;
        shadow_sig["target_price"] = execution_price;
        shadow_sig["expected_return"] = net_expected_return;
        shadow_sig["reason"] = "NO_FUTURE_PERMISSIONS";
        
        TradeLogger::log_simulated_trade(shadow_sig, metrics.atr_14, 1.0, execution_price, execution_price);        return; 
    }

    // Continuous Fractional Kelly Criterion
    double variance = std::pow(metrics.vol_60_annualized, 2);
    double kelly_multiplier = 0.25; 
    
    double f_star = kelly_multiplier * (std::abs(net_expected_return) / variance);
    double cov_penalty = fetch_portfolio_covariance(symbol);
    f_star *= (1.0 - cov_penalty);

    double target_capital = current_balance * std::min(f_star, 0.20); 
    double total_qty = std::floor(target_capital / execution_price);
    
    if (total_qty <= 0) {
        std::cout << "[QUANT EXEC] Kelly size too small for execution on " << symbol << std::endl;
        return;
    }

    std::string sector = get_sector(symbol);
    ApprovalStatus risk_status = risk_manager.validate_kelly_size(sector, total_qty, execution_price);
    
    if (!risk_status.approved) {
        std::cout << "[RISK REJECT] " << symbol << " Kelly size denied. Reason: " << risk_status.reason << std::endl;
        return;
    }
    
    total_qty = risk_status.adjusted_size;
    Contract contract = resolve_contract(symbol);

    std::cout << "\n[QUANT EXEC] Coalesced " << signals.size() << " signals for " << symbol 
              << " | Net Target: " << (net_expected_return * 100) << "%"
              << " | Kelly f*: " << f_star << " | Final Qty: " << total_qty << std::endl;

    risk_manager.record_execution(sector, total_qty * execution_price);

    // Tranching & Execution
    Order parent;
    parent.orderId = nextOrderId++;
    parent.action = side;
    parent.orderType = "MKT"; 
    parent.totalQuantity = Decimal(total_qty);
    parent.transmit = false;

    // Protective Stop
    double stop_dist = 1.5 * metrics.atr_14;
    double hard_stop = (side == "BUY") ? execution_price - stop_dist : execution_price + stop_dist;

    Order stop;
    stop.orderId = nextOrderId++;
    stop.parentId = parent.orderId;
    stop.action = (side == "BUY") ? "SELL" : "BUY";
    stop.orderType = "STP";
    stop.auxPrice = hard_stop;
    stop.totalQuantity = Decimal(total_qty);
    stop.tif = "GTC";
    stop.transmit = true; // Open right tail

    if (paper_mode) {
        std::cout << "[PAPER] EXECUTED " << side << " " << total_qty << " " << symbol << " @ MKT [Stop: " << hard_stop << "]" << std::endl;
        active_positions[symbol] += (side == "BUY") ? total_qty : -total_qty;
    } else {
        client->placeOrder(parent.orderId, contract, parent);
        client->placeOrder(stop.orderId, contract, stop);
    }

    // Register TATS State
    TATSState state;
    state.activated = false;
    state.activation_target = execution_price * (1.0 + net_expected_return);
    state.atr = metrics.atr_14;
    state.original_stop_id = stop.orderId;
    state.side = side;
    state.entry_price = execution_price;
    state.execution_time = std::chrono::system_clock::now().time_since_epoch().count();
    state.lag_minutes = vwal;
    state.current_qty = total_qty;
    state.current_stop = hard_stop; 
    state.take_profit = execution_price * (1.0 + net_expected_return);
    active_tats[symbol] = state;
}

// --------------------------------------------------------------------------
// BLUEPRINT 3: Right-Tail Maximization (TATS & Time Decay)
// --------------------------------------------------------------------------
void ExecutionEngine::evaluate_tats_and_decay(const std::string& symbol, double current_price) {
    if (active_tats.find(symbol) == active_tats.end()) return;
    TATSState& state = active_tats[symbol];

    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    long long elapsed_mins = (now - state.execution_time) / 60000;
    
    bool close_paper = false;
    std::string exit_reason = "";

    // 1. Time Decay Flattening
    if (elapsed_mins > state.lag_minutes * 1.5 && !state.activated) {
        std::cout << "[TIME DECAY] " << symbol << " shock absorbed by market. Liquidating." << std::endl;
        
        if (paper_mode) {
            close_paper = true;
            exit_reason = "TIME_DECAY_FLATTEN";
        } else {
            Contract contract = resolve_contract(symbol);
            Order flatten;
            flatten.orderId = nextOrderId++;
            flatten.action = (state.side == "BUY") ? "SELL" : "BUY";
            flatten.orderType = "MKT";
            flatten.totalQuantity = Decimal(state.current_qty);
            flatten.transmit = true;
            
            client->cancelOrder(state.original_stop_id, "");
            client->placeOrder(flatten.orderId, contract, flatten);
            active_tats.erase(symbol);
            return;
        }
    }

    // 2. Paper Matching Engine (Stops & Targets)
    if (paper_mode && !close_paper) {
        if (state.side == "BUY") {
            if (current_price <= state.current_stop) { close_paper = true; exit_reason = "STOP_LOSS_HIT"; }
            else if (current_price >= state.take_profit) { close_paper = true; exit_reason = "TAKE_PROFIT_HIT"; }
        } else if (state.side == "SELL") {
            if (current_price >= state.current_stop) { close_paper = true; exit_reason = "STOP_LOSS_HIT"; }
            else if (current_price <= state.take_profit) { close_paper = true; exit_reason = "TAKE_PROFIT_HIT"; }
        }
    }

    // 3. Execute Paper Close & Log to DB
    if (close_paper) {
        std::cout << "[PAPER EXIT] " << symbol << " | Reason: " << exit_reason << " | Price: $" << current_price << std::endl;
        
        json log_sig;
        log_sig["symbol"] = symbol;
        log_sig["action"] = "CLOSE";
        log_sig["target_price"] = current_price;
        log_sig["status"] = "PAPER_CLOSED";
        log_sig["reason"] = exit_reason;
        
        TradeLogger::log_simulated_trade(log_sig, 0.0, state.current_qty, current_price, current_price);

        if (state.side == "BUY") active_positions[symbol] -= state.current_qty;
        else active_positions[symbol] += state.current_qty;

        if (active_positions[symbol] <= 0) active_positions.erase(symbol);
        active_tats.erase(symbol);
        return;
    }

    // 4. Live Target-Activated Trailing Stop
    if (!paper_mode && !state.activated) {
        bool target_hit = (state.side == "BUY" && current_price >= state.activation_target) || 
                          (state.side == "SELL" && current_price <= state.activation_target);

        if (target_hit) {
            std::cout << "\n[TATS ACTIVATED] " << symbol << " hit Expected Return threshold (" << state.activation_target << ")." << std::endl;
            std::cout << " -> Canceling hard stop, initiating Volatility Trail (1.5x ATR)." << std::endl;
            
            client->cancelOrder(state.original_stop_id, "");

            Contract contract = resolve_contract(symbol);
            Order trail;
            trail.orderId = nextOrderId++;
            trail.action = (state.side == "BUY") ? "SELL" : "BUY";
            trail.orderType = "TRAIL";
            trail.auxPrice = state.atr * 1.5; 
            trail.totalQuantity = Decimal(state.current_qty);
            trail.tif = "GTC";
            trail.transmit = true;

            client->placeOrder(trail.orderId, contract, trail);
            state.activated = true; 
        }
    }
}

// --------------------------------------------------------------------------
// UTILS & IBKR CALLBACKS (Preserved Unmodified)
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

void ExecutionEngine::tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) {
    std::lock_guard<std::mutex> lock(engine_mtx);
    if (tickerid_to_symbol.find(tickerId) == tickerid_to_symbol.end()) return;
    
    std::string sym = tickerid_to_symbol[tickerId];
    if (field == BID) market_cache[sym].bid = price;
    else if (field == ASK) market_cache[sym].ask = price;
    else if (field == LAST) {
        market_cache[sym].last = price;
        evaluate_tats_and_decay(sym, price);
    }
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