FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpqxx-dev \
    libpq-dev \
    libhiredis-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY ./common /app/common
COPY ./thalamus /app/thalamus

WORKDIR /app/thalamus
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libpqxx-dev \
    libpq5 \
    libhiredis-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/thalamus/build/thalamus_engine .

ENV REDIS_HOST=corpus_callosum
ENV DB_HOST=hippocampus

CMD ["./thalamus_engine"]