cmake_minimum_required(VERSION 3.10)
project(BrainstemEngine)

set(CMAKE_CXX_STANDARD 17)

find_package(nlohmann_json REQUIRED)

# Link to IBKR and Hiredis
include_directories(/usr/local/include/twsapi)
link_directories(/usr/local/lib)

add_executable(brainstem_engine 
    src/main.cpp 
    src/ExecutionEngine.cpp 
    src/utils/ContractResolver.cpp
)

target_link_libraries(brainstem_engine 
    TwsApi 
    hiredis 
    nlohmann_json::nlohmann_json 
    pthread
)