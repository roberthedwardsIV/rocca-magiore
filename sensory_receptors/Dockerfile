FROM gcc:12

RUN apt-get update && apt-get install -y \
    cmake libpq-dev libpqxx-dev libhiredis-dev libmseed-dev libwebsockets-dev \
    libcurl4-openssl-dev nlohmann-json3-dev libboost-system-dev \
    python3 python3-pip ffmpeg nodejs locales git coreutils curl \
    pkg-config libavcodec-dev libavformat-dev libavutil-dev \
    libosmium2-dev libprotozero-dev zlib1g-dev libbz2-dev \
    libswresample-dev libopenblas-dev wget unzip \
    libprotobuf-dev protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/ibkr_build

RUN wget -q http://www.netlib.org/misc/intel/IntelRDFPMathLib20U2.tar.gz && \
    tar -xzf IntelRDFPMathLib20U2.tar.gz && \
    cd IntelRDFPMathLib20U2/LIBRARY && \
    sed -i 's/-Qoption,cpp,--extended_float_types//g' makefile && \
    make CC="gcc -fPIC" && \
    cp libbid.a /usr/lib/ && \
    cp -r src /usr/include/intel_math

SHELL ["/bin/bash", "-c"]

RUN wget -q https://interactivebrokers.github.io/downloads/twsapi_macunix.1019.04.zip -O twsapi.zip && \
    unzip -q twsapi.zip && \
    mkdir -p /tmp/tws_flat && \
    find -L IBJts -name "*.cpp" -exec cp -f {} /tmp/tws_flat/ \; && \
    find -L IBJts -name "*.h" -exec cp -f {} /tmp/tws_flat/ \; && \
    find -L IBJts -name "*.c" -exec cp -f {} /tmp/tws_flat/ \; && \
    find -L IBJts -name "*.proto" -exec cp -n {} /tmp/tws_flat/ \; || true && \
    cd /tmp/tws_flat && \
    if ls *.proto 1> /dev/null 2>&1; then \
        protoc --cpp_out=. *.proto; \
    fi && \
    for file in *.cpp *.cc; do \
        if [[ "$file" == *"Main.cpp"* ]]; then continue; fi; \
        if [[ ! -f "$file" ]]; then continue; fi; \
        g++ -c "$file" \
        -std=c++17 \
        -fPIC \
        -O3 \
        -DIB_USE_STD_STRING \
        -I/usr/include/intel_math \
        -I. ; \
    done && \
    ar rcs libtwsapi.a *.o && \
    cp libtwsapi.a /usr/lib/ && \
    mkdir -p /usr/include/IB && \
    cp *.h /usr/include/IB/ && \
    chmod -R 755 /usr/include/IB && \
    ls -la /usr/include/IB/CommissionReport.h && \
    cd / && rm -rf /tmp/tws_flat /tmp/ibkr_build

SHELL ["/bin/sh", "-c"]

RUN sed -i -e 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen && locale-gen
ENV LANG en_US.UTF-8
ENV LC_ALL en_US.UTF-8
ENV PYTHONUNBUFFERED=1
RUN pip3 install --default-timeout=100 --no-cache-dir yt-dlp --break-system-packages

WORKDIR /tmp
RUN git clone https://github.com/sewenew/redis-plus-plus.git && \
    cd redis-plus-plus && mkdir build && cd build && \
    cmake .. && make && make install && ldconfig

RUN git clone --branch v1.5.4 https://github.com/ggerganov/whisper.cpp.git && \
    cd whisper.cpp && make -j$(nproc) && \
    ar rcs libggml.a ggml.o ggml-alloc.o ggml-backend.o ggml-quants.o && \
    ar rcs libwhisper.a whisper.o && \
    cp libwhisper.a /usr/lib/ && cp libggml.a /usr/lib/ && cp *.h /usr/include/ && \
    bash ./models/download-ggml-model.sh small.en && \
    mkdir -p /app/models && \
    cp models/ggml-small.en.bin /app/models/

WORKDIR /app

COPY ./common /app/common

COPY ./sensory_receptors /app

RUN rm -rf build && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

RUN echo "#!/bin/bash\n\
# Start Sensors\n\
./build/seismic_engine &\n\
./build/aviation_ingest &\n\
./build/ais_ingest &\n\
./build/fire_ingest &\n\
./build/ibkr_ingest &\n\
./build/fred_macro &\n\
./build/filings_ingest &\n\
./build/src/data_sources/med_streams/radio_supervisor &\n\
./build/twitter_recon &\n\
\n\
# Map Ingestion Logic\n\
MAP_FILE=\$(find /shared -name '*.pbf' | head -n 1)\n\
if [ -n \"\$MAP_FILE\" ]; then\n\
    echo \"[SYSTEM] Found map file: \$MAP_FILE. Starting automated ingest...\"\n\
    # Wait for DB to be ready before hammering it with map data\n\
    sleep 10\n\
    ./build/osm_ingest \"\$MAP_FILE\"\n\
else\n\
    echo \"[SYSTEM] No .pbf map file found in /shared. Skipping OSM ingest.\"\n\
fi\n\
\n\
# Keep container alive\n\
wait" > start.sh

RUN chmod +x start.sh
CMD ["./start.sh"]