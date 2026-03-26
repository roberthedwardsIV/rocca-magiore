cmake_minimum_required(VERSION 3.10)
project(BrainstemEngine)

set(CMAKE_CXX_STANDARD 17)

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)

# Use PkgConfig to find hiredis if possible, or link standard
pkg_check_modules(HIREDIS hiredis)
pkg_check_modules(PQXX REQUIRED libpqxx)

# Include directories
include_directories(
    src 
    /usr/include/intel_math  
    /usr/include             
    ${CMAKE_SOURCE_DIR}/common/include
    ${PQXX_INCLUDE_DIRS} 
)

add_executable(brainstem_engine 
    src/main.cpp 
    src/ExecutionEngine.cpp 
    src/utils/ContractResolver.cpp
    src/managers/RiskManager.cpp  
    src/utils/TradeLogger.cpp  
)

target_link_libraries(brainstem_engine 
    twsapi      # Links /usr/lib/libtwsapi.a
    bid         # Links /usr/lib/libbid.a
    hiredis     # Redis client
    nlohmann_json::nlohmann_json 
    pthread
    dl
    m           # Math library (required for Intel Decimal)
    pqxx
    pq
)