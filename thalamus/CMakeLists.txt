cmake_minimum_required(VERSION 3.10)
project(ThalamusEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)

include_directories(
    src 
    src/events
    src/assets 
    src/routes      
    src/hubs        
    src/chokepoints 
    src/finance    
    src/tickers    
    /usr/include/postgresql
)

include_directories(${CMAKE_SOURCE_DIR}/../common/include)

add_executable(thalamus_engine 
    src/main.cpp 
    src/GlobalRegistry.cpp
    src/Dispatcher.cpp
    src/Propagator.cpp
    src/Archiver.cpp
    src/DatabaseManager.cpp

    src/events/EarthquakeTracker.cpp
    src/events/WildfireTracker.cpp  
    
    src/assets/MineAsset.cpp
    src/assets/RefineryAsset.cpp
    src/assets/SmelterAsset.cpp  
    
    src/routes/RoadRoute.cpp
    src/routes/RailRoute.cpp
    src/routes/MaritimeRoute.cpp
    src/routes/AirRoute.cpp
    src/routes/PipelineRoute.cpp
    src/routes/PowerTransmissionRoute.cpp
    src/routes/WaterwayRoute.cpp

    src/hubs/PortHub.cpp
    src/hubs/AirportHub.cpp
    src/hubs/RailNodeHub.cpp
    src/hubs/DistributionHub.cpp
    src/hubs/PowerSubstationHub.cpp
    src/hubs/PowerPlantHub.cpp
    src/hubs/EnergyTerminal.cpp

    src/chokepoints/BridgeChokePoint.cpp
    src/chokepoints/TunnelChokePoint.cpp
    src/chokepoints/BorderChokePoint.cpp
    src/chokepoints/CanalLockChokePoint.cpp
    src/chokepoints/DamChokePoint.cpp
    src/chokepoints/RunwayChokePoint.cpp
    src/chokepoints/PowerSubstationChokePoint.cpp
    src/chokepoints/PortCraneChokePoint.cpp
    src/chokepoints/PumpingStationChokePoint.cpp

    src/finance/SignalEngine.cpp
    src/tickers/TickerRegistry.cpp
    src/tickers/StockTicker.cpp
    src/tickers/FutureTicker.cpp
    src/tickers/OptionTicker.cpp
    src/tickers/CommoditySpotTicker.cpp
)

target_link_libraries(thalamus_engine 
    pqxx 
    pq
    hiredis 
    nlohmann_json::nlohmann_json 
    pthread
)