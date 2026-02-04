FROM gcc:12

# 1. Install Build Dependencies
# We include libprotobuf-dev just in case, though TWS 10.19 usually doesn't need it.
# We include libhiredis-dev and nlohmann-json3-dev for your app logic.
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    libhiredis-dev \
    nlohmann-json3-dev \
    libprotobuf-dev \
    protobuf-compiler \
    wget \
    unzip \
    git \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# FIX: Intel Math Library (Static PIC Strategy) - EXACT COPY FROM SENSORY_RECEPTORS
# ---------------------------------------------------------------------------
WORKDIR /tmp/ibkr_build

# Step A: Build Intel Decimal Math Library
RUN wget -q http://www.netlib.org/misc/intel/IntelRDFPMathLib20U2.tar.gz && \
    tar -xzf IntelRDFPMathLib20U2.tar.gz && \
    cd IntelRDFPMathLib20U2/LIBRARY && \
    # 1. Remove the ICC-specific flag that crashes GCC
    sed -i 's/-Qoption,cpp,--extended_float_types//g' makefile && \
    # 2. BUILD: Force GCC with -fPIC to allow linking into shared/static apps
    make CC="gcc -fPIC" && \
    # 3. Install
    cp libbid.a /usr/lib/ && \
    cp -r src /usr/include/intel_math

# ---------------------------------------------------------------------------
# FIX: TWS API (Downgraded to 10.19.04) - EXACT COPY FROM SENSORY_RECEPTORS
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
    # Cleanup
    cd / && rm -rf /tmp/tws_flat /tmp/ibkr_build

SHELL ["/bin/sh", "-c"]
# ---------------------------------------------------------------------------

# 3. Build Brainstem
WORKDIR /app
COPY . .

RUN rm -rf build && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

CMD ["./build/brainstem_engine"]