#ifndef TICKER_REGISTRY_HPP
#define TICKER_REGISTRY_HPP

#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include "BaseTicker.hpp"

class TickerRegistry {
public:
    static bool initialize_from_db();

    static std::shared_ptr<BaseTicker> get_ticker(const std::string& symbol);
    static void for_each_ticker(std::function<void(std::shared_ptr<BaseTicker>)> func);

private:
    static std::unordered_map<std::string, std::shared_ptr<BaseTicker>> ticker_map;
    static std::mutex ticker_mtx;
};

#endif