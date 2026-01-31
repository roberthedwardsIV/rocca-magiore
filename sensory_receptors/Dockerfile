FROM gcc:12

RUN apt-get update && apt-get install -y \
    cmake libpq-dev libpqxx-dev libhiredis-dev libmseed-dev \
    libcurl4-openssl-dev nlohmann-json3-dev libboost-system-dev \
    python3 python3-pip ffmpeg nodejs locales git coreutils curl \
    pkg-config libavcodec-dev libavformat-dev libavutil-dev \
    libswresample-dev libopenblas-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/InteractiveBrokers/twsapi.git /tmp/twsapi && \
    cd /tmp/twsapi/source/cppclient && \
    mkdir build && cd build && \
    cmake .. && make && make install && ldconfig

RUN curl -fsSL https://deno.land/x/install/install.sh | sh
ENV DENO_INSTALL="/root/.deno"
ENV PATH="$DENO_INSTALL/bin:$PATH"

RUN sed -i -e 's/# en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen && locale-gen
ENV LANG en_US.UTF-8
ENV LC_ALL en_US.UTF-8
ENV PYTHONUNBUFFERED=1
RUN pip3 install --no-cache-dir yt-dlp --break-system-packages

WORKDIR /tmp

RUN git clone https://github.com/sewenew/redis-plus-plus.git && \
    cd redis-plus-plus && mkdir build && cd build && \
    cmake .. && make && make install && ldconfig

RUN git clone --branch v1.5.4 https://github.com/ggerganov/whisper.cpp.git && \
    cd whisper.cpp && \
    make -j$(nproc) && \
    ar rcs libggml.a ggml.o ggml-alloc.o ggml-backend.o ggml-quants.o && \
    ar rcs libwhisper.a whisper.o && \
    cp libwhisper.a /usr/lib/ && \
    cp libggml.a /usr/lib/ && \
    cp *.h /usr/include/ && \
    bash ./models/download-ggml-model.sh small.en && \
    mkdir -p /app/models && \
    cp models/ggml-small.en.bin /app/models/

WORKDIR /app
COPY . .

RUN rm -rf build && mkdir build && cd build && \
    cmake .. && make -j$(nproc)

RUN echo "#!/bin/bash\n\
./build/seismic_engine &\n\
./build/aviation_ingest &\n\
./build/ibkr_ingest &\n\
./build/src/data_sources/med_streams/radio_supervisor &\n\
./build/twitter_recon" > start.sh

RUN chmod +x start.sh
CMD ["./start.sh"]