FROM gcc:12

# 1. Install System Dependencies
# KEEP: libmseed-dev (Seismic), libpqxx (DB), etc.
# ADD: libav* (FFmpeg) and libopenblas-dev (Whisper acceleration)
RUN apt-get update && apt-get install -y \
    cmake libpq-dev libpqxx-dev libhiredis-dev libmseed-dev \
    libcurl4-openssl-dev nlohmann-json3-dev libboost-system-dev \
    python3 python3-pip ffmpeg nodejs locales git coreutils curl \
    pkg-config libavcodec-dev libavformat-dev libavutil-dev \
    libswresample-dev libopenblas-dev \
    && rm -rf /var/lib/apt/lists/*

# 2. Keep Deno (Existing Dependency)
RUN curl -fsSL https://deno.land/x/install/install.sh | sh
ENV DENO_INSTALL="/root/.deno"
ENV PATH="$DENO_INSTALL/bin:$PATH"

# 3. Keep Python/Locales (Existing Dependency)
RUN sed -i -e 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen && locale-gen
ENV LANG en_US.UTF-8
ENV LC_ALL en_US.UTF-8
ENV PYTHONUNBUFFERED=1
RUN pip3 install --no-cache-dir yt-dlp --break-system-packages

WORKDIR /tmp

# 4. Keep Redis++ (Existing Dependency)
RUN git clone https://github.com/sewenew/redis-plus-plus.git && \
    cd redis-plus-plus && mkdir build && cd build && \
    cmake .. && make && make install && ldconfig

RUN git clone --branch v1.5.4 https://github.com/ggerganov/whisper.cpp.git && \
    cd whisper.cpp && \
    # 1. Compile the source code to object files (.o)
    make -j$(nproc) && \
    # 2. MANUALLY package the object files into static libraries.
    #    This guarantees the .a files exist, regardless of Makefile targets.
    ar rcs libggml.a ggml.o ggml-alloc.o ggml-backend.o ggml-quants.o && \
    ar rcs libwhisper.a whisper.o && \
    # 3. Install Libs and Headers
    cp libwhisper.a /usr/lib/ && \
    cp libggml.a /usr/lib/ && \
    cp *.h /usr/include/ && \
    # 4. Download Model
    bash ./models/download-ggml-model.sh small.en && \
    mkdir -p /app/models && \
    cp models/ggml-small.en.bin /app/models/

WORKDIR /app
COPY . .

# 6. Build All Engines (Seismic + Radio)
RUN rm -rf build && mkdir build && cd build && \
    cmake .. && make -j$(nproc)

# 7. Update Startup Script to run Radio Supervisor instead of YT
RUN echo "#!/bin/bash\n\
./build/seismic_engine &\n\
./build/src/data_sources/med_streams/radio_supervisor &\n\
./build/twitter_recon" > start.sh

RUN chmod +x start.sh
CMD ["./start.sh"]