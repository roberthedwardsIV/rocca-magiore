find_package(Boost REQUIRED COMPONENTS system)

add_executable(yt_news_supervisor 
    YtSupervisor.cpp 
    YtStream.cpp
)

target_link_libraries(yt_news_supervisor 
    ${HIREDIS_LIBRARIES} 
    Boost::system 
    pthread
)