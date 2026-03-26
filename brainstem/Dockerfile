FROM gcc:12

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
    libpq-dev \
    libpqxx-dev \
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
    cd / && rm -rf /tmp/tws_flat /tmp/ibkr_build

SHELL ["/bin/sh", "-c"]

WORKDIR /app

COPY ./common /app/common

COPY ./brainstem /app

RUN rm -rf build && mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

CMD ["./build/brainstem_engine"]