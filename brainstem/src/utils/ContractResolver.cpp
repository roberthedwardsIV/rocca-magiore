#include "ContractResolver.hpp"
#include "IB/Contract.h" // <--- CRITICAL FIX: Defines 'Contract' type
#include <iostream>

Contract ContractResolver::resolve(const std::string& internal_symbol, const std::string& type) {
    Contract contract;
    contract.symbol = internal_symbol;
    contract.currency = "USD";

    if (type == "stock") {
        contract.secType = "STK";
        contract.exchange = "SMART";
    } 
    else if (type == "future") {
        contract.secType = "FUT";
        if (internal_symbol == "HG") contract.exchange = "COMEX";
        else if (internal_symbol == "CL") contract.exchange = "NYMEX";
        else contract.exchange = "GLOBEX";
    }
    else if (type == "option") {
        contract.secType = "OPT";
        contract.exchange = "SMART";
    }
    return contract;
}