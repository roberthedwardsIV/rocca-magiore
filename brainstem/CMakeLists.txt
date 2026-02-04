cmake_minimum_required(VERSION 3.10)
project(BrainstemEngine)

set(CMAKE_CXX_STANDARD 17)

find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)

# Use PkgConfig to find hiredis if possible, or link standard
pkg_check_modules(HIREDIS hiredis)

# Include directories
include_directories(
    src 
    /usr/include/intel_math  # For Intel Decimal Math headers
    /usr/include             # For IB headers (in IB/ folder)
)

add_executable(brainstem_engine 
    src/main.cpp 
    src/ExecutionEngine.cpp 
    src/utils/ContractResolver.cpp
    src/utils/TradeLogger.cpp  # Added this source file which was missing in your list
)

target_link_libraries(brainstem_engine 
    twsapi      # Links /usr/lib/libtwsapi.a
    bid         # Links /usr/lib/libbid.a
    hiredis     # Redis client
    nlohmann_json::nlohmann_json 
    pthread
    dl
    m           # Math library (required for Intel Decimal)
)