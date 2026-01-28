cmake_minimum_required(VERSION 3.10)
project(SeismicMonitor)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)  # Added for cpp-httplib/social ingest

# Keep existing checks
pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(LIBAV REQUIRED libavcodec libavformat libavutil libswresample)

include_directories(
    ${PQXX_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${LIBAV_INCLUDE_DIRS}
    /usr/include/postgresql
)

link_directories(${PQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS} ${LIBAV_LIBRARY_DIRS})

# --- EXISTING SEISMIC ENGINE ---
add_executable(seismic_engine src/data_sources/fast_streams/seismic_monitors.cpp)
target_link_libraries(seismic_engine 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    mseed 
    pthread
)

add_executable(aviation_ingest src/data_sources/fast_streams/aviation_ingest.cpp)
target_link_libraries(aviation_ingest
    ${HIREDIS_LIBRARIES}
    curl
    pthread
)
# --- NEW RADIO ENGINE ---
add_subdirectory(src/data_sources/med_streams)

# --- NEW TWITTER RECON ENGINE (Slow Streams) ---
# 1. Define the executable FIRST
add_executable(twitter_recon src/data_sources/slow_streams/twitter_recon_ingest.cpp)

# 2. Link libraries SECOND
target_link_libraries(twitter_recon 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    curl 
    pthread
)