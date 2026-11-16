# Build context (staged by run-matrix.sh): impl-c/ and draft/revised/ (the unit tests read wire-vectors.json).
FROM debian:trixie-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev meson ninja-build python3 pkg-config bash grep \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY impl-c/ impl-c/
COPY draft/revised/ draft/revised/
RUN meson setup build-c impl-c && meson compile -C build-c && meson test -C build-c --print-errorlogs

FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        iproute2 ethtool tcpdump procps \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build-c/linux/schcvxd /usr/local/bin/schcvxd
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
