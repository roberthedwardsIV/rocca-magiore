cmake_minimum_required(VERSION 3.10)
project(SeismicMonitor)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)  # Added for cpp-httplib/social ingest

pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(LIBAV REQUIRED libavcodec libavformat libavutil libswresample)

include_directories(
    ${PQXX_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${LIBAV_INCLUDE_DIRS}
    /usr/include/postgresql
    /usr/local/include/twsapi
)

link_directories(${PQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS} ${LIBAV_LIBRARY_DIRS} /usr/local/lib) 

# --- SEISMIC ENGINE (Fast streams) ---
add_executable(seismic_engine src/data_sources/fast_streams/seismic_monitors.cpp)
target_link_libraries(seismic_engine 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    mseed 
    pthread
)

# --- AVIATION INGEST (Fast streams) ---
add_executable(aviation_ingest src/data_sources/fast_streams/aviation_ingest.cpp)
target_link_libraries(aviation_ingest
    ${HIREDIS_LIBRARIES}
    curl
    pthread
)

# --- MARKET DATA STREAMS ---
add_executable(ibkr_ingest src/market_data/ibkr_ingest.cpp)
target_link_libraries(ibkr_ingest
    ${HIREDIS_LIBRARIES}
    TwsApi
    pthread
)

# --- RADIO ENGINE (Medium Streams) ---
add_subdirectory(src/data_sources/med_streams)

# --- TWITTER RECON ENGINE (Slow Streams) ---
add_executable(twitter_recon src/data_sources/slow_streams/twitter_recon_ingest.cpp)
target_link_libraries(twitter_recon 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    curl 
    pthread
)