cmake_minimum_required(VERSION 3.10)
project(SeismicMonitor)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Keep existing checks
pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# New checks for Radio Engine
pkg_check_modules(LIBAV REQUIRED libavcodec libavformat libavutil libswresample)

include_directories(
    ${PQXX_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${LIBAV_INCLUDE_DIRS}
    /usr/include/postgresql
)

link_directories(${PQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS} ${LIBAV_LIBRARY_DIRS})

# --- EXISTING SEISMIC ENGINE (DO NOT TOUCH) ---
add_executable(seismic_engine src/data_sources/fast_streams/seismic_monitors.cpp)
target_link_libraries(seismic_engine 
    ${PQXX_LIBRARIES} 
    ${HIREDIS_LIBRARIES} 
    mseed 
    pthread
)

# --- NEW RADIO ENGINE ---
add_subdirectory(src/data_sources/med_streams)