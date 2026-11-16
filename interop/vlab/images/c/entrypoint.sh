#!/bin/bash
# Lab entrypoint for the C VTEP. Environment: LOCAL_IP, PEER_IP (underlay), RECEIVER (ordered|tolerant).
set -euo pipefail
: "${LOCAL_IP:?}" "${PEER_IP:?}" "${RECEIVER:=ordered}"
case $RECEIVER in ordered|tolerant) ;; *) echo "bad RECEIVER=$RECEIVER" >&2; exit 2;; esac

wait_if() { for _ in $(seq 1 300); do ip link show "$1" >/dev/null 2>&1 && return 0; sleep 0.2; done
            echo "interface $1 did not appear" >&2; exit 1; }
wait_if eth1; wait_if eth2

mkdir -p /run/schcvx /etc/schcvx
cat > /etc/schcvx/schcvx.conf <<EOC
[global]
max_entries = 4096
max_bytes = 67108864
orphan_budget = 4096
[association lab]
local_ip = $LOCAL_IP
peer_ip = $PEER_IP
udp_port = 60789
src_port = 60789
variant = compact
fr_rule_id = 1
cd_rule_id = 0
vnis = 100
tap = schc0
pmtu = 1280
h_ip = 20
max_frame_len = 1518
max_fragments = 8
receiver = $RECEIVER
S = 1000000
B = 10000000
I = 10000000
Delta = 20000000
R = 100000000
Q = 200000000
max_entries = 1024
max_retired = 65536
max_bytes = 33554432
orphan_budget = 2048
dtag_start = 0
dtag_rate = 1000000
dtag_burst = 10000
counters_file = /run/schcvx/counters
counters_interval = 1000000000
EOC

ip addr add "$LOCAL_IP/24" dev eth2
ip link set eth2 up
ip link set eth1 up
# veth TX checksum offload leaves partial UDP checksums that the daemon's raw socket rejects.
ethtool -K eth2 tx off >/dev/null

schcvxd -c /etc/schcvx/schcvx.conf &
PID=$!
trap 'kill $PID 2>/dev/null' TERM INT
for _ in $(seq 1 100); do ip link show schc0 >/dev/null 2>&1 && break; kill -0 $PID || exit 1; sleep 0.1; done
ip link show schc0 >/dev/null

ip link add br0 type bridge
ip link set eth1 master br0
ip link set schc0 master br0
ip link set schc0 up
ip link set br0 up
echo "schcvxd ready (receiver=$RECEIVER)"
wait $PID
