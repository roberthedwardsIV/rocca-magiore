FROM gcc:12

# 1. Install all system dependencies
RUN apt-get update && apt-get install -y \
    cmake libpq-dev libpqxx-dev libhiredis-dev libmseed-dev libwebsockets-dev \
    libcurl4-openssl-dev nlohmann-json3-dev libboost-system-dev \
    python3 python3-pip ffmpeg nodejs locales git coreutils curl \
    pkg-config libavcodec-dev libavformat-dev libavutil-dev \
    libosmium2-dev libprotozero-dev zlib1g-dev libbz2-dev \
    libswresample-dev libopenblas-dev wget unzip \
    libprotobuf-dev protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# FIX: Intel Math Library (Static PIC Strategy)
# ---------------------------------------------------------------------------
WORKDIR /tmp/ibkr_build

# Step A: Build Intel Decimal Math Library
# We build as a STATIC library (.a) but with Position Independent Code (-fPIC).
# This avoids the complex Shared Object linking errors while still allowing
# it to be linked into your application.
RUN wget -q http://www.netlib.org/misc/intel/IntelRDFPMathLib20U2.tar.gz && \
    tar -xzf IntelRDFPMathLib20U2.tar.gz && \
    cd IntelRDFPMathLib20U2/LIBRARY && \
    # 1. Remove the ICC-specific flag that crashes GCC
    sed -i 's/-Qoption,cpp,--extended_float_types//g' makefile && \
    # 2. BUILD: Force GCC with -fPIC.
    #    This overrides the makefile's 'icc' default and ensures the static
    #    library contains relocatable code for your final build.
    make CC="gcc -fPIC" && \
    # 3. Install the static archive
    cp libbid.a /usr/lib/ && \
    cp -r src /usr/include/intel_math

# ---------------------------------------------------------------------------
# FIX: TWS API (Downgraded to 10.19.04)
# ---------------------------------------------------------------------------
SHELL ["/bin/bash", "-c"]

RUN wget -q https://interactivebrokers.github.io/downloads/twsapi_macunix.1019.04.zip -O twsapi.zip && \
    unzip -q twsapi.zip && \
    mkdir -p /tmp/tws_flat && \
    # 1. Flatten Source
    find -L IBJts -name "*.cpp" -exec cp -f {} /tmp/tws_flat/ \; && \
    find -L IBJts -name "*.h" -exec cp -f {} /tmp/tws_flat/ \; && \
    find -L IBJts -name "*.c" -exec cp -f {} /tmp/tws_flat/ \; && \
    find -L IBJts -name "*.proto" -exec cp -n {} /tmp/tws_flat/ \; || true && \
    cd /tmp/tws_flat && \
    # 2. Generate Protobufs (if present)
    if ls *.proto 1> /dev/null 2>&1; then \
        protoc --cpp_out=. *.proto; \
    fi && \
    # 3. Compile
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
    # 4. Build & Install
    ar rcs libtwsapi.a *.o && \
    cp libtwsapi.a /usr/lib/ && \
    mkdir -p /usr/include/IB && \
    cp *.h /usr/include/IB/ && \
    chmod -R 755 /usr/include/IB && \
    # DEBUG: Check for the file. In 10.19.04, this MUST succeed.
    ls -la /usr/include/IB/CommissionReport.h && \
    # Cleanup
    cd / && rm -rf /tmp/tws_flat /tmp/ibkr_build

SHELL ["/bin/sh", "-c"]
# ---------------------------------------------------------------------------

# 3. Setup Locale & Python
RUN sed -i -e 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen && locale-gen
ENV LANG en_US.UTF-8
ENV LC_ALL en_US.UTF-8
ENV PYTHONUNBUFFERED=1
RUN pip3 install --default-timeout=100 --no-cache-dir yt-dlp --break-system-packages

WORKDIR /tmp
RUN git clone https://github.com/sewenew/redis-plus-plus.git && \
    cd redis-plus-plus && mkdir build && cd build && \
    cmake .. && make && make install && ldconfig

# 4. Whisper CPP Build
RUN git clone --branch v1.5.4 https://github.com/ggerganov/whisper.cpp.git && \
    cd whisper.cpp && make -j$(nproc) && \
    ar rcs libggml.a ggml.o ggml-alloc.o ggml-backend.o ggml-quants.o && \
    ar rcs libwhisper.a whisper.o && \
    cp libwhisper.a /usr/lib/ && cp libggml.a /usr/lib/ && cp *.h /usr/include/ && \
    bash ./models/download-ggml-model.sh small.en && \
    mkdir -p /app/models && \
    cp models/ggml-small.en.bin /app/models/

# 5. Final Application Build
WORKDIR /app
COPY . .
RUN rm -rf build && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

RUN echo "#!/bin/bash\n\
./build/seismic_engine &\n\
./build/aviation_ingest &\n\
./build/ais_ingest &\n\
./build/fire_ingest &\n\
./build/ibkr_ingest &\n\
./build/fred_macro &\n\
./build/filings_ingest &\n\
./build/src/data_sources/med_streams/radio_supervisor &\n\
./build/twitter_recon" > start.sh

RUN chmod +x start.sh
CMD ["./start.sh"]