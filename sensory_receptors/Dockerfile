FROM gcc:12

# 1. Cleaned up APT dependencies (Removed libprotobuf-dev and protobuf-compiler)
RUN apt-get update && apt-get install -y \
    cmake libpq-dev libpqxx-dev libhiredis-dev libmseed-dev libwebsockets-dev \
    libcurl4-openssl-dev nlohmann-json3-dev libboost-system-dev \
    python3 python3-pip ffmpeg nodejs locales git coreutils curl \
    pkg-config libavcodec-dev libavformat-dev libavutil-dev \
    libosmium2-dev libprotozero-dev zlib1g-dev libbz2-dev \
    libswresample-dev libopenblas-dev wget unzip \
    && rm -rf /var/lib/apt/lists/*

# --- REMOVED: Intel Math Library Build Block ---
# --- REMOVED: TWS API Build Block ---

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

# --- START SCRIPT (Already verified) ---
RUN echo "#!/bin/bash\n\
# Start Sensors\n\
./build/geojson &\n\
./build/seismic_engine &\n\
./build/fire_ingest &\n\
#./build/aviation_ingest &\n\
#./build/ais_ingest &\n\
#./build/fred_macro &\n\
\n\
# Keep container alive and wait for background processes\n\
wait" > start.sh

RUN chmod +x start.sh
CMD ["./start.sh"]