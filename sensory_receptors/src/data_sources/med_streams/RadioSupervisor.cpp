#include "RadioStream.hpp"
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <iostream>


// Main(): initalizes Global AV Network + channels to stream, then starts threads for each of them
int main() {
    avformat_network_init();
    av_log_set_level(AV_LOG_QUIET);

    // Stations to listen to:
    std::vector<std::unique_ptr<RadioStream>> receptors;
    receptors.push_back(std::make_unique<RadioStream>("BBC_News", "GB", "news"));
    receptors.push_back(std::make_unique<RadioStream>("NPR_News", "US", "news"));
    receptors.push_back(std::make_unique<RadioStream>("AlJazeera", "QA", "news"));

    std::cout << "[RADIO SUPERVISOR]: Initializing " << receptors.size() << " receptors..." << std::endl;

    for (auto& r : receptors) {
        r->start();
    }
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
    return 0;
}