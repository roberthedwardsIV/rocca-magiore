cmake_minimum_required(VERSION 3.10)
project(SeismicMonitor)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(HIREDIS REQUIRED hiredis)

include_directories(${PQXX_INCLUDE_DIRS} ${HIREDIS_INCLUDE_DIRS})
link_directories(${PQXX_LIBRARY_DIRS} ${HIREDIS_LIBRARY_DIRS})

add_executable(seismic_engine src/seismic_ingest.cpp)

target_link_libraries(seismic_engine ${PQXX_LIBRARIES} ${HIREDIS_LIBRARIES} pthread)

add_subdirectory(src/data_sources/med_streams)