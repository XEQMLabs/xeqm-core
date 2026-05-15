# =============================================================================
# XEQM Node Docker Image
# Multistage build — builder compiles a fully statically-linked daemon, runtime
# is a minimal Ubuntu 24.04 with only ca-certificates and curl for healthchecks.
# =============================================================================

# --- Builder stage ---
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# BUILD_STATIC_DEPS=ON downloads and compiles Boost, OpenSSL, libsodium, libzmq,
# libcurl, libsqlite3, libgmp, libzstd, etc. from source. The toolchain we need
# for that is build-essential + cmake + git + perl + autotools + python3. The
# dev packages for those libraries are NOT needed (we build them ourselves) but
# we retain libreadline-dev for wallet line editing and libhidapi-dev/libusb-1.0
# for hardware wallet support.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    perl \
    autoconf \
    automake \
    libtool \
    m4 \
    python3 \
    ca-certificates \
    curl \
    libreadline-dev \
    libhidapi-dev \
    libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN git submodule update --init --recursive

RUN mkdir -p build && cd build && \
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_STATIC_DEPS=ON \
        -DBoost_NO_BOOST_CMAKE=ON \
        .. && \
    make -j"$(nproc)" daemon simplewallet wallet_rpc_server

# Confirm the binaries are portably linked. If anything beyond the standard
# glibc/libstdc++ runtime set leaks in, fail the build now rather than ship a
# broken image to operators.
RUN set -e; \
    ALLOW='^[[:space:]]*(linux-vdso\.so|libc\.so|libstdc\+\+\.so|libgcc_s\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|/lib(64)?/ld-linux)'; \
    for bin in xeqm-d xeqm-wallet xeqm-rpc; do \
        echo "=== $bin ==="; \
        ldd "build/bin/$bin" || true; \
        unexpected=$(ldd "build/bin/$bin" | awk '{print $1}' | grep -Ev "$ALLOW" || true); \
        if [ -n "$unexpected" ]; then \
            echo "FAIL: $bin has non-portable runtime dependencies:"; \
            echo "$unexpected"; \
            exit 1; \
        fi; \
    done; \
    echo "All binaries are portably linked."

# --- Runtime stage ---
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# The static binaries only need ca-certificates (TLS roots for outbound HTTPS,
# e.g. seed peer DNS) and curl (for operator healthchecks).
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/bin/xeqm-d /usr/local/bin/xeqm-d
COPY --from=builder /src/build/bin/xeqm-wallet /usr/local/bin/xeqm-wallet
COPY --from=builder /src/build/bin/xeqm-rpc /usr/local/bin/xeqm-rpc

RUN chmod +x /usr/local/bin/xeqm-d /usr/local/bin/xeqm-wallet /usr/local/bin/xeqm-rpc

WORKDIR /data

# 9230: P2P, 9231: RPC, 9232: quorumnet, 9233: storage server (if used)
EXPOSE 9230 9231 9232 9233

ENTRYPOINT ["/usr/local/bin/xeqm-d"]
