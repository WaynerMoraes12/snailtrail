FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ clang clang-format clang-tidy lld \
        cmake ninja-build git ca-certificates \
        libgtest-dev \
        python3-dev python3-pybind11 python3-pip python3-venv python3-pytest \
        g++-mingw-w64-x86-64-posix \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
