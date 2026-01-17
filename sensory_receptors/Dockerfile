FROM gcc:12

# Install system dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    libpq-dev \
    libpqxx-dev \
    libhiredis-dev \
    libmseed-dev \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp
RUN git clone https://github.com/sewenew/redis-plus-plus.git && \
    cd redis-plus-plus && \
    mkdir build && \
    cd build && \
    cmake .. && \
    make && \
    make install && \
    ldconfig

WORKDIR /app

COPY . .

RUN g++ -O3 -I/usr/include/postgresql src/data_sources/fast_streams/seismic_monitors.cpp -o seismic_monitors \
    -lpqxx -lpq -lhiredis -lmseed -pthread

RUN g++ -O3 src/data_sources/fast_streams/geojson.cpp -o geojson \
    -L/usr/local/lib \
    -I/usr/local/include \
    -lcurl -lredis++ -lhiredis -pthread

# RUN g++ -O3 src/aviation_ingest.cpp -o aviation_ingest -lcurl -lhiredis
# RUN g++ -O3 src/weather_fore_ingest.cpp -o weather_fore_ingest -lcurl -lhiredis
# RUN g++ -O3 -I/usr/include/postgresql src/signal_processing/nexus_core.cpp -o nexus_core -lpqxx -lpq -lhiredis

RUN echo "#!/bin/bash\n\
./seismic_monitors &\n\
./geojson" > start.sh

RUN chmod +x start.sh

CMD ["./start.sh"]