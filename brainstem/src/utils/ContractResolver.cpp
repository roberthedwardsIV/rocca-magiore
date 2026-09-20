#include "ContractResolver.hpp"
#include "IB/Contract.h"
#include <iostream>

Contract ContractResolver::resolve(const std::string& internal_symbol, const std::string& type) {
    Contract contract;
    contract.symbol = internal_symbol;
    contract.currency = "USD";

    // Enforce strict matching for Futures to avoid IBKR order rejection
    if (type == "future" || internal_symbol == "HG" || internal_symbol == "CL" || internal_symbol == "GC") {
        contract.secType = "FUT";
        
        // REQUIRED: IBKR must have an expiration date for live orders. 
        // This must match exactly how we ingest the market data in ibkr_ingest.cpp
        contract.lastTradeDateOrContractMonth = "202612"; 
        
        if (internal_symbol == "HG" || internal_symbol == "GC") {
            contract.exchange = "COMEX";
        }
        else if (internal_symbol == "CL") {
            contract.exchange = "NYMEX";
        }
        else {
            contract.exchange = "GLOBEX";
        }
    }
    else if (type == "option") {
        contract.secType = "OPT";
        contract.exchange = "SMART";
    }
    else {
        // Default to stock for equities and unhandled spot mappings
        contract.secType = "STK";
        contract.exchange = "SMART";
    }
    
    return contract;
}