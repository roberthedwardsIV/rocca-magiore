#ifndef BASE_TICKER_HPP
#define BASE_TICKER_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <mutex>
#include <unordered_map>

using json = nlohmann::json;

class BaseTicker {
public:
    std::string symbol;
    std::string instrument_type; 
    std::string exchange;

    std::unordered_map<std::string, float> sensitivity_vector;

    BaseTicker(std::string sym, std::string type, std::string exch) 
        : symbol(sym), instrument_type(type), exchange(exch) {}

    virtual ~BaseTicker() = default;

    virtual void process_quote(const json& quote) = 0;

    virtual json get_json_state() const = 0;

protected:
    mutable std::mutex ticker_mutex;
};

#endif