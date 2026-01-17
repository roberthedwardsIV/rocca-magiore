# --- STAGE 1: Compile ---
FROM debian:bookworm-slim AS builder

# Install build tools and development libraries
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpqxx-dev \
    libpq-dev \
    libhiredis-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the source files
COPY . .

# Create build directory and compile
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# --- STAGE 2: Runtime ---
FROM debian:bookworm-slim

# Install only the runtime versions of the libraries
RUN apt-get update && apt-get install -y \
    libpqxx-dev \
    libpq-dev \
    libhiredis0.14 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy only the compiled binary from the builder stage
COPY --from=builder /app/build/thalamus_engine .

# Use the new service names as default environment variables
ENV REDIS_HOST=corpus_callosum
ENV DB_HOST=hippocampus

# Run the binary
CMD ["./thalamus_engine"]