# =============================================================================
# Equilibria Node Docker Image
# Multistage build — builder compiles the daemon, runtime is minimal Ubuntu 24.04
# =============================================================================

# --- Builder stage ---
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    libssl-dev \
    libzmq3-dev \
    libsodium-dev \
    libsqlite3-dev \
    libboost-all-dev \
    libcurl4-openssl-dev \
    libuv1-dev \
    libgmp-dev \
    ca-certificates \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN git submodule update --init --recursive

RUN mkdir -p build && cd build && \
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DARCH=x86-64 \
        .. && \
    make -j$(nproc) daemon

# --- Runtime stage ---
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libzmq5 \
    libsodium23 \
    libsqlite3-0 \
    libboost-system1.83.0 \
    libboost-filesystem1.83.0 \
    libboost-thread1.83.0 \
    libboost-program-options1.83.0 \
    libboost-serialization1.83.0 \
    libcurl4 \
    libuv1 \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/bin/xeq-d /usr/local/bin/xeq-d
COPY --from=builder /src/build/bin/xeq-wallet-cli /usr/local/bin/xeq-wallet-cli
COPY --from=builder /src/build/bin/xeq-wallet-rpc /usr/local/bin/xeq-wallet-rpc

RUN chmod +x /usr/local/bin/xeq-d /usr/local/bin/xeq-wallet-cli /usr/local/bin/xeq-wallet-rpc

WORKDIR /data

EXPOSE 9230 9231 9232 9233

ENTRYPOINT ["/usr/local/bin/xeq-d"]
