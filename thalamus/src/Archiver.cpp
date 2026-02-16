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

        // Fixed loop for Routes
        GlobalRegistry::for_each_route([](std::shared_ptr<BaseRoute> route) {
            save_to_database(route->get_json_state());
        });

        // Added loop for Hubs
        GlobalRegistry::for_each_hub([](std::shared_ptr<BaseHub> hub) {
            save_to_database(hub->get_json_state());
        });

        // Added loop for Chokepoints
        GlobalRegistry::for_each_chokepoint([](std::shared_ptr<BaseChokePoint> cp) {
            save_to_database(cp->get_json_state());
        });

        TickerRegistry::for_each_ticker([](std::shared_ptr<BaseTicker> ticker) {
            save_ticker_state(ticker->get_json_state());
        });
        
        std::cout << "[SNAPSHOTTER] Infrastructure state sync complete." << std::endl;
    }
}