/**
* Archiver.cpp: runs the reaper (that archives stale signals by saving them to db)
*               and runs the snapshotter (that takes a global state snapshot every
*               5 minutes)
*/
#include "Archiver.hpp"
#include "GlobalRegistry.hpp"
#include "DatabaseManager.hpp"
#include "tickers/TickerRegistry.hpp"
#include <chrono>
#include <thread>
#include <iostream>

void Archiver::run_reaper() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(10));
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::vector<std::string> to_remove;

        GlobalRegistry::for_each_event([&](const std::string& id, std::shared_ptr<BaseEvent> event) {
            if (event->is_stale(now)) {
                save_to_database(event->to_json());
                to_remove.push_back(id);
            }
        });

        for (const auto& id : to_remove) {
            std::cout << "[REAPER] Archiving stale event: " << id << std::endl;
            GlobalRegistry::remove_event(id);
        }
    }
}

void Archiver::run_snapshotter() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        
        GlobalRegistry::for_each_asset([](std::shared_ptr<BaseAsset> asset) {
            save_to_database(asset->get_json_state());
        });

        GlobalRegistry::for_each_supply_line([](std::shared_ptr<BaseSupplyLine> line) {
            save_to_database(line->get_json_state());
        });
        
        TickerRegistry::for_each_ticker([](std::shared_ptr<BaseTicker> ticker) {
            save_ticker_state(ticker->get_json_state());
        });
        
        std::cout << "[SNAPSHOTTER] Infrastructure state sync complete." << std::endl;
    }
}