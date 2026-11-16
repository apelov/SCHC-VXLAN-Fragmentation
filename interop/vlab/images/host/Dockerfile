# Base is trixie: the bookworm apt keyring in the current bookworm images fails signature checks.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        iperf3 iputils-ping iproute2 tcpdump jq ca-certificates \
    && rm -rf /var/lib/apt/lists/*
CMD ["sleep", "infinity"]
