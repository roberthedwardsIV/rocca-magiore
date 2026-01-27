#include "RadioStream.hpp"
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <iostream>

int main() {
    // 1. Initialize Global AV Network
    avformat_network_init();
    av_log_set_level(AV_LOG_QUIET); // Silence FFmpeg logs

    std::vector<std::unique_ptr<RadioStream>> receptors;

    // 2. Define Stations (Name, CountryCode, Tag)
    // The resolver will find the best stream URL automatically
    receptors.push_back(std::make_unique<RadioStream>("BBC_News", "GB", "news"));
    receptors.push_back(std::make_unique<RadioStream>("NPR_News", "US", "news"));
    receptors.push_back(std::make_unique<RadioStream>("AlJazeera", "QA", "news"));

    std::cout << "[RADIO SUPERVISOR]: Initializing " << receptors.size() << " receptors..." << std::endl;

    for (auto& r : receptors) {
        r->start();
    }

    // Keep main thread alive
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    return 0;
}