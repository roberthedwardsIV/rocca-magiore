cmake_minimum_required(VERSION 3.10)
project(SeismicMonitor)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(LIBAV REQUIRED libavcodec libavformat libavutil libswresample)

# Include directories for your project
include_directories(
    ${PQXX_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${LIBAV_INCLUDE_DIRS}
    /usr/include/postgresql
    # The Dockerfile installed TWS headers here:
    /usr/include/intel_math
    /usr/include
)

# The Dockerfile installed libs to /usr/lib, which is in the default path,
# but we add /usr/local/lib just in case.
link_directories(${PQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS} ${LIBAV_LIBRARY_DIRS} /usr/local/lib)

# --- SEISMIC ENGINE ---
add_executable(seismic_engine src/data_sources/fast_streams/seismic_monitors.cpp)
target_link_libraries(seismic_engine 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    mseed 
    pthread
)

# --- AVIATION INGEST ---
add_executable(aviation_ingest src/data_sources/fast_streams/aviation_ingest.cpp)
target_link_libraries(aviation_ingest
    ${HIREDIS_LIBRARIES}
    curl
    pthread
    m
)

# --- MARITIME AIS INGEST ---
add_executable(ais_ingest src/data_sources/fast_streams/ais_ingest.cpp)
target_link_libraries(ais_ingest 
    ${HIREDIS_LIBRARIES} 
    websockets
    ssl
    crypto
    pthread
)

# --- WILDFIRE INGEST ---
add_executable(fire_ingest src/data_sources/slow_streams/fire_ingest.cpp)
target_link_libraries(fire_ingest 
    ${HIREDIS_LIBRARIES} 
    curl 
    pthread
)
# --- FILINGS INGEST ---
add_executable(filings_ingest src/data_sources/slow_streams/filings_ingest.cpp)
target_link_libraries(filings_ingest 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    curl 
    pthread
)

# --- MARKET DATA STREAMS (IBKR) ---
add_executable(ibkr_ingest src/market_data/ibkr_ingest.cpp)
target_link_libraries(ibkr_ingest
    # Link the pre-built libraries we made in Docker
    twsapi      # This matches libtwsapi.a
    bid         # This matches libbid.so
    protobuf    # Required by TWS 10.37+
    ${PQXX_LIBRARIES}
    ${HIREDIS_LIBRARIES}
    pthread
    m
    dl
)

add_executable(fred_macro src/market_data/FRED_macro.cpp)
target_link_libraries(fred_macro
    ${HIREDIS_LIBRARIES}
    curl
    nlohmann_json::nlohmann_json
    pthread
)

# --- RADIO ENGINE ---
add_subdirectory(src/data_sources/med_streams)

# --- TWITTER RECON ENGINE ---
add_executable(twitter_recon src/data_sources/slow_streams/twitter_recon_ingest.cpp)
target_link_libraries(twitter_recon 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    curl 
    pthread
)