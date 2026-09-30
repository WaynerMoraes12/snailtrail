FROM debian:trixie-slim AS toolchain
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake ninja-build libgtest-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

FROM toolchain AS build
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY core ./core
COPY cli ./cli
COPY tests ./tests
COPY samples ./samples
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSNAILTRAIL_WARNINGS_AS_ERRORS=ON \
        -DSNAILTRAIL_STATIC=ON \
    && cmake --build /build \
    && ctest --test-dir /build --output-on-failure -j "$(nproc)"

FROM debian:trixie-slim AS cli
COPY --from=build /build/cli/snailtrail /usr/local/bin/snailtrail
RUN useradd --uid 10001 --no-create-home --shell /usr/sbin/nologin snailtrail
USER snailtrail
WORKDIR /work
ENTRYPOINT ["snailtrail"]
CMD ["help"]

FROM python:3.13-slim AS wheel
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ cmake ninja-build \
    && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir build
WORKDIR /src
COPY pyproject.toml CMakeLists.txt ./
COPY cmake ./cmake
COPY core ./core
COPY bindings ./bindings
COPY python ./python
RUN python -m build --wheel --outdir /wheels

FROM python:3.13-slim AS app
COPY --from=wheel /wheels /wheels
RUN pip install --no-cache-dir "$(ls /wheels/*.whl)[dashboard,lab]" && rm -rf /wheels
COPY --from=build /build/cli/snailtrail /usr/local/bin/snailtrail
RUN useradd --uid 10001 --create-home snailtrail
USER snailtrail
WORKDIR /home/snailtrail
EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=3s --start-period=20s --retries=3 \
    CMD ["python", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/api/health', timeout=2)"]
CMD ["snailtrail-dashboard"]
