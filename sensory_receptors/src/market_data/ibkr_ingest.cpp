#include "IB/EWrapper.h"
#include "IB/EClientSocket.h"
#include "IB/EReader.h"           
#include "IB/EReaderOSSignal.h"   
#include "IB/Contract.h"
#include "IB/Order.h"
#include "IB/OrderState.h"
#include "IB/Execution.h"
#include "IB/ScannerSubscription.h"
#include "IB/CommissionReport.h"
#include "IB/CommonDefs.h"
#include <hiredis/hiredis.h>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <set>
#include <atomic>
#include <unordered_set>
#include <ctime>
#include <cstdlib>

using json = nlohmann::json;

// --- STUB CLASS FOR EWRAPPER INTERFACE ---
class EWrapperStub : public EWrapper {
public:
    void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override {}
    void tickSize(TickerId tickerId, TickType field, Decimal size) override {}
    void tickOptionComputation(TickerId tickerId, TickType tickType, int tickAttrib, double impliedVol, double delta, double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice) override {}
    void tickGeneric(TickerId tickerId, TickType tickType, double value) override {}
    void tickString(TickerId tickerId, TickType tickType, const std::string& value) override {}
    void tickEFP(TickerId tickerId, TickType tickType, double basisPoints, const std::string& formattedBasisPoints, double totalDividends, int holdDays, const std::string& futureLastTradeDate, double dividendImpact, double dividendsToLastTradeDate) override {}
    void orderStatus(OrderId orderId, const std::string& status, Decimal filled, Decimal remaining, double avgFillPrice, int permId, int parentId, double lastFillPrice, int clientId, const std::string& whyHeld, double mktCapPrice) override {}
    void openOrder(OrderId orderId, const Contract&, const Order&, const OrderState&) override {}
    void openOrderEnd() override {}
    void winError(const std::string& str, int lastError) override {}
    void connectionClosed() override {}
    void updateAccountValue(const std::string& key, const std::string& val, const std::string& currency, const std::string& accountName) override {}
    void updatePortfolio(const Contract& contract, Decimal position, double marketPrice, double marketValue, double averageCost, double unrealizedPNL, double realizedPNL, const std::string& accountName) override {}
    void updateAccountTime(const std::string& timeStamp) override {}
    void accountDownloadEnd(const std::string& accountName) override {}
    void nextValidId(OrderId orderId) override {}
    void contractDetails(int reqId, const ContractDetails& contractDetails) override {}
    void bondContractDetails(int reqId, const ContractDetails& contractDetails) override {}
    void contractDetailsEnd(int reqId) override {}
    void execDetails(int reqId, const Contract& contract, const Execution& execution) override {}
    void execDetailsEnd(int reqId) override {}
    void error(int id, int errorCode, const std::string& errorString, const std::string& advancedOrderRejectJson) override {}
    void updateMktDepth(TickerId id, int position, int operation, int side, double price, Decimal size) override {}
    void updateMktDepthL2(TickerId id, int position, const std::string& marketMaker, int operation, int side, double price, Decimal size, bool isSmartDepth) override {}
    void updateNewsBulletin(int msgId, int msgType, const std::string& newsMessage, const std::string& originExch) override {}
    void managedAccounts(const std::string& accountsList) override {}
    void receiveFA(faDataType pFaDataType, const std::string& cxml) override {}
    void historicalData(TickerId reqId, const Bar& bar) override {}
    void historicalDataEnd(int reqId, const std::string& startDateStr, const std::string& endDateStr) override {}
    void scannerParameters(const std::string& xml) override {}
    void scannerData(int reqId, int rank, const ContractDetails& contractDetails, const std::string& distance, const std::string& benchmark, const std::string& projection, const std::string& legsStr) override {}
    void scannerDataEnd(int reqId) override {}
    void realtimeBar(TickerId reqId, long time, double open, double high, double low, double close, Decimal volume, Decimal wap, int count) override {}
    void currentTime(long time) override {}
    void fundamentalData(TickerId reqId, const std::string& data) override {}
    void deltaNeutralValidation(int reqId, const DeltaNeutralContract& deltaNeutralContract) override {}
    void tickSnapshotEnd(int reqId) override {}
    void marketDataType(TickerId reqId, int marketDataType) override {}
    void commissionReport(const CommissionReport& commissionReport) override {}
    void position(const std::string& account, const Contract& contract, Decimal position, double avgCost) override {}
    void positionEnd() override {}
    void accountSummary(int reqId, const std::string& account, const std::string& tag, const std::string& value, const std::string& curency) override {}
    void accountSummaryEnd(int reqId) override {}
    void verifyMessageAPI(const std::string& apiData) override {}
    void verifyCompleted(bool isSuccessful, const std::string& errorText) override {}
    void displayGroupList(int reqId, const std::string& groups) override {}
    void displayGroupUpdated(int reqId, const std::string& contractInfo) override {}
    void verifyAndAuthMessageAPI(const std::string& apiData, const std::string& xyzChallange) override {}
    void verifyAndAuthCompleted(bool isSuccessful, const std::string& errorText) override {}
    void connectAck() override {}
    void positionMulti(int reqId, const std::string& account, const std::string& modelCode, const Contract& contract, Decimal pos, double avgCost) override {}
    void positionMultiEnd(int reqId) override {}
    void accountUpdateMulti(int reqId, const std::string& account, const std::string& modelCode, const std::string& key, const std::string& value, const std::string& currency) override {}
    void accountUpdateMultiEnd(int reqId) override {}
    void securityDefinitionOptionalParameter(int reqId, const std::string& exchange, int underlyingConId, const std::string& tradingClass, const std::string& multiplier, const std::set<std::string>& expirations, const std::set<double>& strikes) override {}
    void securityDefinitionOptionalParameterEnd(int reqId) override {}
    void softDollarTiers(int reqId, const std::vector<SoftDollarTier>& tiers) override {}
    void familyCodes(const std::vector<FamilyCode>& familyCodes) override {}
    void symbolSamples(int reqId, const std::vector<ContractDescription>& contractDescriptions) override {}
    void mktDepthExchanges(const std::vector<DepthMktDataDescription>& depthMktDataDescriptions) override {}
    void tickNews(int tickerId, time_t timeStamp, const std::string& providerCode, const std::string& articleId, const std::string& headline, const std::string& extraData) override {}
    void smartComponents(int reqId, const SmartComponentsMap& theMap) override {}
    void tickReqParams(int tickerId, double minTick, const std::string& bboExchange, int snapshotPermissions) override {}
    void newsProviders(const std::vector<NewsProvider>& newsProviders) override {}
    void newsArticle(int requestId, int articleType, const std::string& articleText) override {}
    void historicalNews(int requestId, const std::string& time, const std::string& providerCode, const std::string& articleId, const std::string& headline) override {}
    void historicalNewsEnd(int requestId, bool hasMore) override {}
    void headTimestamp(int reqId, const std::string& headTimestamp) override {}
    void histogramData(int reqId, const HistogramDataVector& data) override {}
    void historicalDataUpdate(TickerId reqId, const Bar& bar) override {}
    void rerouteMktDataReq(int reqId, int conid, const std::string& exchange) override {}
    void rerouteMktDepthReq(int reqId, int conid, const std::string& exchange) override {}
    void marketRule(int marketRuleId, const std::vector<PriceIncrement>& priceIncrements) override {}
    void pnl(int reqId, double dailyPnL, double unrealizedPnL, double realizedPnL) override {}
    void pnlSingle(int reqId, Decimal pos, double dailyPnL, double unrealizedPnL, double realizedPnL, double value) override {}
    void historicalTicks(int reqId, const std::vector<HistoricalTick>& ticks, bool done) override {}
    void historicalTicksBidAsk(int reqId, const std::vector<HistoricalTickBidAsk>& ticks, bool done) override {}
    void historicalTicksLast(int reqId, const std::vector<HistoricalTickLast>& ticks, bool done) override {}
    void tickByTickAllLast(int reqId, int tickType, time_t time, double price, Decimal size, const TickAttribLast& tickAttribLast, const std::string& exchange, const std::string& specialConditions) override {}
    void tickByTickBidAsk(int reqId, time_t time, double bidPrice, double askPrice, Decimal bidSize, Decimal askSize, const TickAttribBidAsk& tickAttribBidAsk) override {}
    void tickByTickMidPoint(int reqId, time_t time, double midPoint) override {}
    void orderBound(long long orderId, int apiClientId, int apiOrderId) override {}
    void completedOrder(const Contract& contract, const Order& order, const OrderState& orderState) override {}
    void completedOrdersEnd() override {}
    void replaceFAEnd(int reqId, const std::string& text) override {}
    void wshMetaData(int reqId, const std::string& dataJson) override {}
    void wshEventData(int reqId, const std::string& dataJson) override {}
    void historicalSchedule(int reqId, const std::string& startDateTime, const std::string& endDateTime, const std::string& timeZone, const std::vector<HistoricalSession>& sessions) override {}
    void userInfo(int reqId, const std::string& whiteBrandingId) override {}
};

class MarketDataFeed : public EWrapperStub {
public:
    std::atomic<bool> is_ready{false}; // Handshake flag

    void refresh_subscriptions() {
        if (client->isConnected() && is_ready) {
            load_targets_and_subscribe();
        }
    }

    MarketDataFeed() : OSSignal(2000) {
        redis_ctx = redisConnect("corpus_callosum", 6379);
        if (!redis_ctx || redis_ctx->err) {
            std::cerr << "[IKBR MARKET FEED] Redis Connection Error: " << (redis_ctx ? redis_ctx->errstr : "Alloc failure") << std::endl;
        }
        // Seed the random number generator
        std::srand(std::time(nullptr));
    }

    ~MarketDataFeed() {
        if (redis_ctx) redisFree(redis_ctx);
    }

    bool connect() {
        client = std::make_unique<EClientSocket>(this, &OSSignal);
        int dynamic_client_id = 100 + (std::rand() % 9899);

        // Explicitly use 127.0.0.1 to force IPv4 routing within the shared network namespace
        if (client->eConnect("127.0.0.1", 4002, dynamic_client_id)) {
            std::cout << "[MARKET FEED] TCP Connected to IBKR Gateway on port 4002 with Client ID: " << dynamic_client_id << std::endl;

            client->reqMarketDataType(3); 
            
            reader = std::make_unique<EReader>(client.get(), &OSSignal);
            reader->start();

            std::thread([this]() {
                while (client->isConnected()) {
                    OSSignal.waitForSignal();
                    reader->processMsgs();
                }
            }).detach();
            
            return true;
        } 
        
        return false;
    }

    // Capture the handshake from the API
    void nextValidId(OrderId orderId) override {
        is_ready = true; // Unlocks the connection block
        std::cout << "[IKBR MARKET FEED] Handshake Complete. Ready to subscribe." << std::endl;
    }

    void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override {
        std::string symbol = id_map[tickerId];
        
        // --- TEMPORARY CATCH-ALL LOG ---
        // Print every single price tick received to see exactly what IBKR is broadcasting out-of-hours
        std::cout << "[MARKET FEED DEBUG] " << symbol << " received TickType: " << field << " | Price: $" << price << std::endl;

        // Handle Live (1=Bid, 2=Ask, 4=Last, 9=Close) and Delayed (66=Bid, 67=Ask, 68=Last, 75=Delayed Close)
        if (field == 1 || field == 2 || field == 4 || field == 9 || field == 66 || field == 67 || field == 68 || field == 75) { 
            long long ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            json j;
            j["entity_type"] = "ticker_update";
            j["symbol"] = symbol;
            
            // Treat Close/Delayed Close the same as Last price for internal logic
            if (field == 4 || field == 68 || field == 9 || field == 75) j["price"] = price;
            if (field == 1 || field == 66) j["bid"] = price;
            if (field == 2 || field == 67) j["ask"] = price;
            
            j["timestamp"] = ts;
            
            if (redis_ctx) {
                std::string payload = j.dump();
                redisCommand(redis_ctx, "LPUSH raw_signals %s", payload.c_str());
            }

            // --- WAKE UP & VALUATE ASSETS ---
            if (ticker_to_assets.find(symbol) != ticker_to_assets.end()) {
                for (int aid : ticker_to_assets[symbol]) {
                    json a_sig;
                    a_sig["asset_id"] = aid;
                    a_sig["category"] = "market";
                    a_sig["price"] = price;
                    a_sig["timestamp"] = ts;
                    if (redis_ctx) {
                        std::string a_payload = a_sig.dump();
                        redisCommand(redis_ctx, "LPUSH raw_signals %s", a_payload.c_str());
                    }
                }
            }
        }
    }
    
    void tickGeneric(TickerId tickerId, TickType tickType, double value) override {
        if (tickType == 24) { 
             std::string symbol = id_map[tickerId];
             json j;
             j["entity_type"] = "ticker_update";
             j["symbol"] = symbol;
             j["volatility"] = value;
             j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
             if (redis_ctx) {
                 std::string payload = j.dump();
                 redisCommand(redis_ctx, "LPUSH raw_signals %s", payload.c_str());
             }
        }
    }

    void tickOptionComputation(TickerId tickerId, TickType tickType, int tickAttrib, double impliedVol, double delta, double optPrice, double pvDividend, double gamma, double vega, double theta, double undPrice) override {
        if (tickType == 13) {
            std::string symbol = id_map[tickerId];
            json j;
            j["entity_type"] = "ticker_update";
            j["symbol"] = symbol;
            if (impliedVol > 0) j["iv"] = impliedVol;
            if (delta > -2) j["delta"] = delta;
            if (gamma > -2) j["gamma"] = gamma;
            if (theta > -2) j["theta"] = theta;
            j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (redis_ctx) {
                std::string payload = j.dump();
                redisCommand(redis_ctx, "LPUSH raw_signals %s", payload.c_str());
            }
        }
    }

    void error(int id, int errorCode, const std::string& errorMsg, const std::string& advancedOrderRejectJson) override {
        // MUTE LOGS: Silence expected/benign API connection messages and known data limitations
        if (errorCode == 2104 || errorCode == 2106 || errorCode == 2158) return; // OK connections
        if (errorCode == 502) return; // Booting lag
        if (errorCode == 2119 || errorCode == 2103) return; // Data farm connection drops (normal on paper)
        if (errorCode == 10197) return; // Competing session (Muted to stop terminal spam)
        if (errorCode == 200) return; // No security def (Usually Pink Sheets/OTC, ignore and move on)
        if (errorCode == 10167) return; // <--- NEW: Mute "Displaying delayed market data" warning
        
        std::cerr << "[IBKR ERROR] Id: " << id << " Code: " << errorCode << " Msg: " << errorMsg << std::endl;
    }

    void wake_up_infrastructure() {
        try {
            pqxx::connection C("dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432");
            pqxx::work W(C);
            long long ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            auto dispatch_pulse = [&](pqxx::result& R, const std::string& id_key, const std::string& category) {
                for (auto row : R) {
                    json j; 
                    if (id_key == "line_id" || id_key == "hub_id") j[id_key] = row[0].as<long long>();
                    else j[id_key] = row[0].as<int>();
                    j["category"] = category; 
                    j["timestamp"] = ts;
                    std::string p = j.dump(); 
                    if (redis_ctx) redisCommand(redis_ctx, "LPUSH raw_signals %s", p.c_str());
                }
            };

            pqxx::result R_routes = W.exec("SELECT line_id FROM supply_lines");
            dispatch_pulse(R_routes, "line_id", "flow");

            pqxx::result R_hubs = W.exec("SELECT id FROM supply_hubs");
            dispatch_pulse(R_hubs, "hub_id", "inventory");

            pqxx::result R_chokes = W.exec("SELECT id FROM supply_chokepoints");
            dispatch_pulse(R_chokes, "cp_id", "maintenance");

            std::cout << "[IKBR MARKET FEED] Emitted Global Infrastructure Wake-up Pulse." << std::endl;
        } catch (...) {}
    }
    
private:
    EReaderOSSignal OSSignal;
    std::unique_ptr<EClientSocket> client;
    std::unique_ptr<EReader> reader;
    redisContext* redis_ctx;
    std::unordered_map<int, std::string> id_map;
    std::unordered_set<std::string> subscribed_symbols;
    std::unordered_map<std::string, std::vector<int>> ticker_to_assets;
    int current_req_id = 1000;

    void load_targets_and_subscribe() {
        try {
            pqxx::connection C("dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432");
            pqxx::work W(C);
            pqxx::result R = W.exec("SELECT symbol, instrument_type, exchange, metadata FROM ticker_registry WHERE active = TRUE");
            std::cout << "[IKBR MARKET FEED] Loading " << R.size() << " targets from DB..." << std::endl;
            
            for (auto row : R) {
                Contract c;
                c.symbol = row["symbol"].as<std::string>();
                if (subscribed_symbols.find(c.symbol) != subscribed_symbols.end()) {
                    continue;
                }
                c.exchange = row["exchange"].as<std::string>();
                c.currency = "USD";
                std::string type = row["instrument_type"].as<std::string>();
                
                if (type == "future") {
                    c.secType = "FUT";
                    c.lastTradeDateOrContractMonth = "202612";
                } else if (type == "commodity_spot") {
                    // TRANSLATE PSEUDO-SPOTS TO VALID IBKR DATA FEEDS
                    if (c.symbol == "XAU_USD") { 
                        c.symbol = "XAUUSD"; c.secType = "CMDTY"; c.exchange = "SMART"; 
                    }
                    else if (c.symbol == "LME_CU") { 
                        c.symbol = "HG"; c.secType = "FUT"; c.exchange = "COMEX"; c.lastTradeDateOrContractMonth = "202612"; 
                    }
                    else if (c.symbol == "WTI_SPOT") { 
                        c.symbol = "CL"; c.secType = "FUT"; c.exchange = "NYMEX"; c.lastTradeDateOrContractMonth = "202612"; 
                    }
                    else if (c.symbol == "LITH_CARB") { 
                        c.symbol = "LIT"; c.secType = "STK"; c.exchange = "SMART"; 
                    }
                } else if (type == "option") {
                    c.secType = "OPT";
                } else {
                    c.secType = "STK";
                    c.exchange = "SMART"; 
                    std::string db_exch = row["exchange"].as<std::string>();
                    // Only set primaryExchange if it's a specific physical exchange (e.g. NYSE)
                    // Otherwise, leave it empty so IBKR's SmartRouter handles it automatically.
                    if (db_exch != "SMART" && db_exch != "Unknown") {
                        c.primaryExchange = db_exch;
                    }
                }
                
                client->reqMktData(current_req_id, c, "100,101,106", false, false, TagValueListSPtr());
                
                // Track it so we don't request it again next cycle
                id_map[current_req_id] = c.symbol; 
                subscribed_symbols.insert(c.symbol);
                
                std::cout << "[IKBR MARKET FEED] -> Subscribed: " << c.symbol << " (" << c.secType << ")" << std::endl;
                
                current_req_id++;
            }
            pqxx::result R_sens = W.exec("SELECT ticker_symbol, entity_id FROM ticker_sensitivity WHERE entity_id LIKE 'ASSET_%'");
            for (auto row : R_sens) {
                std::string sym = row["ticker_symbol"].as<std::string>();
                std::string ent = row["entity_id"].as<std::string>();
                try {
                    int asset_id = std::stoi(ent.substr(6)); // Strip "ASSET_"
                    ticker_to_assets[sym].push_back(asset_id);
                } catch (...) {}
            }
            std::cout << "[IKBR MARKET FEED] Loaded " << ticker_to_assets.size() << " asset-to-ticker sensitivity mappings." << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "[DB ERROR] Failed to load targets: " << e.what() << std::endl;
        }
    }

    
};

int main() {
    MarketDataFeed feed;
    
    // Single connection loop to prevent double connection attempts
    while (true) {
        if (feed.connect()) {
            break; 
        }
        std::cerr << "[MARKET FEED] Connection failed. Retrying in 10 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    
    std::cout << "[MARKET FEED] Waiting for IBKR API Handshake..." << std::endl;
    while (!feed.is_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // 3. Handshake received! Load the first batch of targets
    feed.refresh_subscriptions();
    feed.wake_up_infrastructure();
    // 4. Main polling loop (check DB for new tickers every 60s)
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        feed.refresh_subscriptions();
    }
    
    return 0;
}