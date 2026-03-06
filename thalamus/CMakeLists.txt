cmake_minimum_required(VERSION 3.10)
project(ThalamusEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)

include_directories(
    src 
    /usr/include/postgresql
    ${CMAKE_SOURCE_DIR}/../common/include
)

add_executable(thalamus_engine 
    src/main.cpp 
    src/Dispatcher.cpp
    src/DatabaseManager.cpp
    src/SignalEngine.cpp
)

target_link_libraries(thalamus_engine 
    pqxx 
    pq
    hiredis 
    nlohmann_json::nlohmann_json 
    pthread
)