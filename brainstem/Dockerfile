FROM gcc:12 AS builder

RUN apt-get update && apt-get install -y \
    cmake git libhiredis-dev nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt
# Download and build IBKR TWS API
RUN git clone https://github.com/InteractiveBrokers/twsapi.git && \
    cd twsapi/source/cppclient && \
    mkdir build && cd build && \
    cmake .. && make && make install

WORKDIR /app
COPY . .

RUN mkdir build && cd build && \
    cmake .. && make -j$(nproc)

CMD ["./build/brainstem_engine"]