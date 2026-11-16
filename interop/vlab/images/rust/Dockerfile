# Build context (staged by run-matrix.sh via git archive origin/main interop/impl-rust): interop/impl-rust/
FROM rust:1-bookworm AS build
WORKDIR /src
COPY interop/impl-rust/ .
RUN cargo build --release

FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        iproute2 ethtool tcpdump procps \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/target/release/vtep /usr/local/bin/vtep
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
