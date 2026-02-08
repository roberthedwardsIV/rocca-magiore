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
    MarketDataFeed() : client(new EClientSocket(this, &OSSignal)), reader(client.get(), &OSSignal) {
        redis_ctx = redisConnect("corpus_callosum", 6379);
        if (!redis_ctx || redis_ctx->err) {
            std::cerr << "[IKBR MARKET FEED] Redis Connection Error: " << (redis_ctx ? redis_ctx->errstr : "Alloc failure") << std::endl;
        }
    }

    ~MarketDataFeed() {
        if (redis_ctx) redisFree(redis_ctx);
    }

    void connect() {
        //    Port 4001 is standard for Gateway; use 7496 if using TWS Live, 7497 for TWS Paper
        if (client->eConnect("192.168.1.164", 4001, 100)) {
            std::cout << "[MARKET FEED] Connected to IBKR Gateway." << std::endl;

            // --- CONFIGURATION FOR MIXED DATA PERMISSIONS ---
            // Type 1 = Live Streaming (Requires full subscriptions)
            // Type 3 = Delayed (15-20 min lag, usually free)
            // Type 4 = Delayed-Frozen (Best for testing; gives delayed live + static close data)
            // CURRENT SETTING: Type 3 (Delayed)

            client->reqMarketDataType(3); 
            std::cout << "[MARKET FEED] Data Mode: DELAYED (Type 3) - Ensuring Futures Data Flow." << std::endl;
            
            // FUTURE UPGRADE: 
            // client->reqMarketDataType(1); 
            // ------------------------------------------------

            std::thread([this]() {
                while (client->isConnected()) {
                    OSSignal.waitForSignal();
                    reader.processMsgs();
                }
            }).detach();
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            load_targets_and_subscribe();

        } else {
            std::cerr << "[MARKET FEED] IBKR Connection Failed. Is the Gateway running?" << std::endl;
        }
    }

    void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override {
        if (field == 4 || field == 1 || field == 2) { 
            std::string symbol = id_map[tickerId];
            json j;
            j["entity_type"] = "ticker_update";
            j["symbol"] = symbol;
            if (field == 4) j["price"] = price;
            if (field == 1) j["bid"] = price;
            if (field == 2) j["ask"] = price;
            j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (redis_ctx) {
                std::string payload = j.dump();
                redisCommand(redis_ctx, "LPUSH raw_signals %s", payload.c_str());
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
    if (errorCode == 2104 || errorCode == 2106) return;
    std::cerr << "[IBKR ERROR] Id: " << id << " Code: " << errorCode << " Msg: " << errorMsg << std::endl;
}
    
private:
    std::unique_ptr<EClientSocket> client;
    EReaderOSSignal OSSignal;
    EReader reader; 
    redisContext* redis_ctx;
    std::unordered_map<int, std::string> id_map;

    void load_targets_and_subscribe() {
        try {
            pqxx::connection C("dbname=rocco_commodities user=rocco_admin password=REMOVED host=hippocampus port=5432");
            pqxx::work W(C);
            pqxx::result R = W.exec("SELECT symbol, instrument_type, exchange, metadata FROM ticker_registry WHERE active = TRUE");
            int reqId = 1000;
            std::cout << "[IKBR MARKET FEED] Loading " << R.size() << " targets from DB..." << std::endl;
            for (auto row : R) {
                Contract c;
                c.symbol = row["symbol"].as<std::string>();
                c.exchange = row["exchange"].as<std::string>();
                c.currency = "USD";
                std::string type = row["instrument_type"].as<std::string>();
                if (type == "future") {
                    c.secType = "FUT";
                    c.lastTradeDateOrContractMonth = "202612";
                } else if (type == "option") {
                    c.secType = "OPT";
                } else {
                    c.secType = "STK";
                    c.exchange = "SMART"; 
                    c.primaryExchange = row["exchange"].as<std::string>();
                }
                client->reqMktData(reqId, c, "100,101,106", false, false, TagValueListSPtr());
                id_map[reqId] = c.symbol;
                std::cout << "   -> Subscribed: " << c.symbol << " (" << type << ")" << std::endl;
                reqId++;
            }
        } catch (const std::exception &e) {
            std::cerr << "[DB ERROR] Failed to load targets: " << e.what() << std::endl;
        }
    }
};

int main() {
    MarketDataFeed feed;
    feed.connect();
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    return 0;
}