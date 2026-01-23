#include "YtStream.hpp"
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <iostream>

int main() {
    std::vector<std::unique_ptr<YtStream>> receptors;

    // Streams to listen to (UPDATE!!!):
    receptors.push_back(std::make_unique<YtStream>("CNN", "https://www.youtube.com/watch?v=EXAMPLE_CNN"));
    receptors.push_back(std::make_unique<YtStream>("AL_JAZEERA", "https://www.youtube.com/watch?v=gCNeDWCI0vo"));
    receptors.push_back(std::make_unique<YtStream>("SKY_NEWS", "https://www.youtube.com/watch?v=EXAMPLE_SKY"));




    std::cout << "[YOUTUBE SUPERVISOR]: Initializing " << receptors.size() << " news receptors..." << std::endl;

    for (auto& r : receptors) {
        r->start();
    }

    // Health monitor loop
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));

        for (auto& r : receptors) {
            if (!r->is_active()) {
                // could implement a restart logic here.
                std::cerr << "[Supervisor]: Warning - Receptor state inactive. Attempting recovery..." << std::endl;
                r->start(); 
            }
        }
    }

    return 0;
}