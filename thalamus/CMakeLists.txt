cmake_minimum_required(VERSION 3.10)
project(ThalamusEngine)

# Set C++ standard to 17 (needed for shared_ptr and modern mutex handling)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find Dependencies
find_package(nlohmann_json REQUIRED)

# 2. Include Directories (for .hpp files)
include_directories(src /usr/include/postgresql)

# 3. Define the Executable and include all source files
# If you eventually add MineTracker.cpp, add it to this list
add_executable(thalamus_engine 
    src/main.cpp 
    src/EarthquakeTracker.cpp 
    src/DatabaseManager.cpp
)

# 4. Link Libraries
# We link hiredis manually as it often doesn't have a CMake find_package script
target_link_libraries(thalamus_engine 
    pqxx 
    pq
    hiredis 
    nlohmann_json::nlohmann_json 
    pthread
)