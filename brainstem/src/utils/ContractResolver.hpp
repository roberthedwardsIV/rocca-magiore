#ifndef CONTRACT_RESOLVER_HPP
#define CONTRACT_RESOLVER_HPP

#include "IB/Contract.h"
#include <string>

class ContractResolver {
public:
    static Contract resolve(const std::string& internal_symbol, const std::string& type = "stock");
};

#endif