FROM ubuntu:24.04

# Set environment to avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies (without boost)
RUN set -ex && \
    apt-get update && \
    apt-get install -y curl apt-transport-https software-properties-common && \
    add-apt-repository ppa:ubuntu-toolchain-r/test && \
    apt-get update && \
    apt-get --no-install-recommends --yes install \
        ca-certificates \
        cmake \
        g++-11 \
        gcc-11 \
        make \
        pkg-config \
        graphviz \
        doxygen \
        git \
        libtool-bin \
        autoconf \
        automake \
        bzip2 \
        xsltproc \
        gperf \
        wget \
        libgmp-dev \
        libgmp3-dev \
        libzstd-dev \
        zlib1g-dev \
        libcurl4-openssl-dev \
        libsystemd-dev \
        libsodium-dev && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100

RUN set -ex && \
    cd /tmp && \
    wget https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz && \
    tar -xf gmp-6.3.0.tar.xz && \
    cd gmp-6.3.0 && \
    ./configure --enable-cxx && \
    make -j$(nproc) && \
    make install

ARG OPENSSL_VERSION=1.1.1g
ARG OPENSSL_HASH=ddb04774f1e32f0c49751e21b67216ac87852ceb056b75209af2443400636d46

RUN set -ex && \
    wget https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz && \
    echo "${OPENSSL_HASH}  openssl-${OPENSSL_VERSION}.tar.gz" | sha256sum -c && \
    tar xf openssl-${OPENSSL_VERSION}.tar.gz && \
    cd openssl-${OPENSSL_VERSION} && \
    ./Configure --prefix=/usr linux-x86_64 no-shared --static && \
    make -j$(nproc) && \
    make install_sw -j$(nproc)

ARG BOOST_VERSION=1_83_0
ARG BOOST_VERSION_DOT=1.83.0
ARG BOOST_HASH=6478edfe2f3305127cffe8caf73ea0176c53769f4bf1585be237eb30798c3b8e

RUN set -ex && \
    wget -O boost_${BOOST_VERSION}.tar.bz2 https://sourceforge.net/projects/boost/files/boost/${BOOST_VERSION_DOT}/boost_${BOOST_VERSION}.tar.bz2 && \
    echo "${BOOST_HASH}  boost_${BOOST_VERSION}.tar.bz2" | sha256sum -c && \
    tar xf boost_${BOOST_VERSION}.tar.bz2 && \
    cd boost_${BOOST_VERSION} && \
    ./bootstrap.sh && \
    ./b2 --prefix=/usr --build-type=minimal link=shared runtime-link=shared \
        --with-atomic --with-chrono --with-date_time --with-filesystem --with-program_options \
        --with-regex --with-serialization --with-system --with-thread --with-locale \
        threading=multi threadapi=pthread cxxflags=-fPIC \
        -j$(nproc) install && \
    ldconfig

# Check if bin directory exists, otherwise show helpful error
RUN if [ ! -d "bin" ]; then \
        echo ""; \
        echo "ERROR: bin directory not found in build context!"; \
        echo ""; \
        echo "Please copy the binaries before building:"; \
        echo "  cp -r ../build/bin ./bin"; \
        echo "  docker build -t equilibria-node ."; \
        echo ""; \
        echo "Or run from project root:"; \
        echo "  docker build -t equilibria-node -f testnet/Dockerfile ."; \
        echo "  (and update COPY to use build/bin/*)"; \
        echo ""; \
        exit 1; \
    fi

# Copy all binaries
COPY bin/* /usr/local/bin/
RUN chmod +x /usr/local/bin/*

# Create directories
RUN mkdir -p /data /wallets /scripts
WORKDIR /data

# Expose port range for all services
EXPOSE 18080-18200

# Default to daemon but allow override
ENTRYPOINT ["/usr/local/bin/xeqm-d"]
