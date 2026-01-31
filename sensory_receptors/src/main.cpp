#include <iostream>
#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include "IB/EWrapper.h"
#include "IB/EClientSocket.h"
#include "IB/Contract.h"

using json = nlohmann::json;

class MarketIngest : public EWrapper {
public:
    MarketIngest() : client(new EClientSocket(this)) {
        redis_ctx = redisConnect("corpus_callosum", 6379);
    }

    void tickPrice(TickerId tickerId, TickType field, double price, const TickAttrib& attrib) override {
        if (field == 1 || field == 2 || field == 4) { // Bid, Ask, Last
            json j;
            j["symbol"] = id_to_symbol[tickerId];
            j["price"] = price;
            j["field"] = field;
            j["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
            
            redisCommand(redis_ctx, "PUBLISH market_ticks %s", j.dump().c_str());
        }
    }

    // Boilerplate EWrapper overrides...
    void nextValidId(OrderId orderId) override { start_subscriptions(); }
    void error(int id, int errorCode, const std::string& errorMsg) override {
        std::cerr << "[IBKR INGEST] Error: " << errorMsg << std::endl;
    }

    void connect() { client->eConnect("ibkr_gateway", 4001, 1); }

private:
    std::unique_ptr<EClientSocket> client;
    redisContext* redis_ctx;
    std::map<TickerId, std::string> id_to_symbol;

    void start_subscriptions() {
        // This would loop through your TickerRegistry list from DB
        Contract c;
        c.symbol = "HG"; c.secType = "FUT"; c.exchange = "COMEX"; c.currency = "USD";
        client->reqMktData(1001, c, "", false, false, TagValueListSPtr());
        id_to_symbol[1001] = "HG";
    }
};

int main() {
    MarketIngest ingest;
    ingest.connect();
    // Message processing loop...
}