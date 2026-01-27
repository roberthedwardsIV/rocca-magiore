# VVV FILE: ./sensory_receptors/src/data_sources/med_streams/CMakeLists.txt VVV

# 1. HARDCODE LIBRARY PATHS (Guaranteed by the manual 'ar' command above)
set(WHISPER_LIB "/usr/lib/libwhisper.a")
set(GGML_LIB "/usr/lib/libggml.a")

add_executable(radio_supervisor 
    RadioSupervisor.cpp 
    RadioStream.cpp
)

target_link_libraries(radio_supervisor 
    ${HIREDIS_LIBRARIES}
    curl
    avformat
    avcodec
    avutil
    swresample
    ${WHISPER_LIB}
    ${GGML_LIB}
    openblas
    pthread
    dl
)