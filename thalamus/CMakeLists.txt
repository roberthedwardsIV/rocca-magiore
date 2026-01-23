cmake_minimum_required(VERSION 3.10)
project(ThalamusEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)

include_directories(
    src 
    src/events
    src/assets 
    src/supply_lines 
    /usr/include/postgresql
)

add_executable(thalamus_engine 
    src/main.cpp 
    src/GlobalRegistry.cpp
    src/Dispatcher.cpp
    src/Propagator.cpp
    src/Archiver.cpp
    src/DatabaseManager.cpp
    # Events
    src/events/EarthquakeTracker.cpp
    # Assets - ADD EVERY SUBCLASS CPP HERE
    src/assets/MineAsset.cpp
    src/assets/RefineryAsset.cpp
    # Supply Lines - ADD EVERY SUBCLASS CPP HERE
    src/supply_lines/RailLine.cpp
    src/supply_lines/RailYard.cpp
    src/supply_lines/MaritimeRoute.cpp
    src/supply_lines/MaritimePort.cpp
    src/supply_lines/PipelineLine.cpp
    src/supply_lines/PipelineStation.cpp
    src/supply_lines/Highway.cpp
    src/supply_lines/Airport.cpp
    src/supply_lines/Airspace.cpp
    src/supply_lines/CanalRoute.cpp
    src/supply_lines/CanalLock.cpp
)

target_link_libraries(thalamus_engine 
    pqxx 
    pq
    hiredis 
    nlohmann_json::nlohmann_json 
    pthread
)