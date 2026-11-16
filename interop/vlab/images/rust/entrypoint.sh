#!/bin/bash
# Lab entrypoint for the Rust VTEP. Environment: LOCAL_IP, PEER_IP (underlay), RECEIVER (ordered|tolerant).
# The Rust vtep captures and injects frames on --lan-if itself: it creates no TAP and needs no bridge.
set -euo pipefail
: "${LOCAL_IP:?}" "${PEER_IP:?}" "${RECEIVER:=ordered}"
case $RECEIVER in ordered|tolerant) ;; *) echo "bad RECEIVER=$RECEIVER" >&2; exit 2;; esac

wait_if() { for _ in $(seq 1 300); do ip link show "$1" >/dev/null 2>&1 && return 0; sleep 0.2; done
            echo "interface $1 did not appear" >&2; exit 1; }
wait_if eth1; wait_if eth2

mkdir -p /etc/schcvx
# Lab values mapped as far as vtep.conf.example allows (see interop/vlab/README.md for the unmapped ones).
cat > /etc/schcvx/vtep.conf <<EOC
lan-if = eth1
local = $LOCAL_IP
port = 60789
peer = $PEER_IP
pmtu = 1280
h-ip = 20
variant = compact
rule = 1
vni = 100
trace = false
receiver = $RECEIVER
inactivity-ms = 10
deadline-ms = 20
retirement-ms = 100
max-entries = 1024
allow = 192.168.44.0/24
EOC

ip addr add "$LOCAL_IP/24" dev eth2
ip link set eth2 up
ip link set eth1 up
ethtool -K eth2 tx off >/dev/null

: > /var/log/vtep.log
vtep --config /etc/schcvx/vtep.conf >>/var/log/vtep.log 2>&1 &
PID=$!
tail -F /var/log/vtep.log &
trap 'kill $PID 2>/dev/null' TERM INT
sleep 0.5; kill -0 $PID
echo "vtep ready (receiver=$RECEIVER)"
wait $PID
