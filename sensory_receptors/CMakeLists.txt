cmake_minimum_required(VERSION 3.10)
project(SeismicMonitor)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(ZLIB REQUIRED)  
find_package(BZip2 REQUIRED)
find_package(EXPAT REQUIRED)
find_package(CURL REQUIRED)

pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(HIREDIS REQUIRED hiredis)
pkg_check_modules(LIBAV REQUIRED libavcodec libavformat libavutil libswresample)

find_path(OSMIUM_INCLUDE_DIR osmium/osm.hpp)
if (NOT OSMIUM_INCLUDE_DIR)
    message(FATAL_ERROR "Osmium headers not found! Ensure libosmium2-dev is installed.")
endif()

include_directories(
    ${PQXX_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${LIBAV_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
    ${OSMIUM_INCLUDE_DIRS}
    ${EXPAT_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    /usr/include/postgresql
    /usr/include/intel_math
    /usr/include
)

link_directories(${PQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS} ${LIBAV_LIBRARY_DIRS} /usr/local/lib)


# --- SEISMIC ENGINE ---
add_executable(seismic_engine src/data_sources/fast_streams/seismic_monitors.cpp)
target_link_libraries(seismic_engine ${PQXX_LIBRARIES} ${HIREDIS_LIBRARIES} mseed pthread)

# --- AVIATION INGEST ---
add_executable(aviation_ingest src/data_sources/fast_streams/aviation_ingest.cpp)
target_link_libraries(aviation_ingest ${HIREDIS_LIBRARIES} curl pthread m)

# --- MARITIME AIS INGEST ---
add_executable(ais_ingest src/data_sources/fast_streams/ais_ingest.cpp)
target_link_libraries(ais_ingest ${HIREDIS_LIBRARIES} websockets ssl crypto pthread)

# --- WILDFIRE INGEST ---
add_executable(fire_ingest src/data_sources/slow_streams/fire_ingest.cpp)
target_link_libraries(fire_ingest ${HIREDIS_LIBRARIES} curl pthread)

# --- FILINGS INGEST ---
add_executable(filings_ingest src/data_sources/slow_streams/filings_ingest.cpp)
target_link_libraries(filings_ingest ${PQXX_LIBRARIES} ${HIREDIS_LIBRARIES} curl pthread)

# --- TWITTER RECON ENGINE ---
add_executable(twitter_recon src/data_sources/slow_streams/twitter_recon_ingest.cpp)
target_link_libraries(twitter_recon ${PQXX_LIBRARIES} ${HIREDIS_LIBRARIES} curl pthread)

# --- MARKET DATA STREAMS (IBKR) ---
add_executable(ibkr_ingest src/market_data/ibkr_ingest.cpp)
target_link_libraries(ibkr_ingest twsapi bid protobuf ${PQXX_LIBRARIES} ${HIREDIS_LIBRARIES} pthread m dl)

# --- MACRO DATA STREAMS (FRED) ---
add_executable(fred_macro src/market_data/FRED_macro.cpp)
target_link_libraries(fred_macro ${HIREDIS_LIBRARIES} curl nlohmann_json::nlohmann_json pthread)

# --- DISCOVERY (OSM) ---
add_executable(osm_ingest src/discovery/osm_ingest.cpp)
target_link_libraries(osm_ingest ${PQXX_LIBRARIES} ${CURL_LIBRARIES} nlohmann_json::nlohmann_json pthread)

# --- RADIO ENGINE ---
add_subdirectory(src/data_sources/med_streams)