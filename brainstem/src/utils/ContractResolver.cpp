Contract ContractResolver::resolve(const std::string& internal_symbol, const std::string& type) {
    Contract contract;
    contract.symbol = internal_symbol;
    contract.currency = "USD";

    if (type == "stock") {
        contract.secType = "STK";
        contract.exchange = "SMART"; // IBKR Smart Routing
    } 
    else if (type == "future") {
        contract.secType = "FUT";
        // Map common commodity codes to their primary exchanges
        if (internal_symbol == "HG") contract.exchange = "COMEX"; // Copper
        else if (internal_symbol == "CL") contract.exchange = "NYMEX"; // Crude
        else contract.exchange = "GLOBEX";
    }
    else if (type == "option") {
        contract.secType = "OPT";
        contract.exchange = "SMART";
        // Options require multiplier, strike, and expiry - loaded from TickerRegistry meta
    }
    return contract;
}