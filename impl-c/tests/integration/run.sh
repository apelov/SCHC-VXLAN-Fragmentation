#!/usr/bin/env bash
# Linux network-namespace integration test for schcvxd (WP10).
# Usage: run.sh /path/to/schcvxd     (must run as root)
set -euo pipefail

BIN=${1:?usage: run.sh /path/to/schcvxd}
[[ $EUID -eq 0 ]] || { echo "must run as root" >&2; exit 2; }
[[ -x $BIN ]] || { echo "not executable: $BIN" >&2; exit 2; }
BIN=$(readlink -f "$BIN")
command -v ethtool >/dev/null || { echo "ethtool not on PATH (needed to disable veth TX checksum offload)" >&2; exit 2; }

P="schcvx$$-"
TMP=$(mktemp -d /tmp/schcvx-it.XXXXXX)
UNDERLAY_IF=u1          # vtep1 underlay veth end (netem is applied here)
declare -A PIDS=()      # daemon pids by vtep number
IPERF_PID=""
FAILS=0
MODE=""

# ---------- reporting ----------
pass() { echo "PASS [$MODE] $*"; }
fail() { echo "FAIL [$MODE] $*"; FAILS=$((FAILS + 1)); }
info() { echo "INFO [$MODE] $*"; }

# ---------- helpers ----------
ns() { local n=$1; shift; ip netns exec "$P$n" "$@"; }

cleanup() {
    set +e
    [[ -n $IPERF_PID ]] && kill "$IPERF_PID" 2>/dev/null
    local n
    for n in "${!PIDS[@]}"; do kill "${PIDS[$n]}" 2>/dev/null; done
    sleep 0.2
    for n in vtep1 vtep2 host1 host2; do ip netns del "$P$n" 2>/dev/null; done
    echo "Temporary directory: $TMP"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

wait_for() { # timeout_s command...
    local t=$1 i; shift
    for ((i = 0; i < t * 20; i++)); do "$@" && return 0; sleep 0.05; done
    return 1
}

# ---------- topology ----------
build_topology() {
    local n
    for n in vtep1 vtep2 host1 host2; do
        ip netns add "$P$n"
        ns "$n" ip link set lo up
    done
    # underlay
    ip link add u1 type veth peer name u2
    ip link set u1 netns "${P}vtep1"; ip link set u2 netns "${P}vtep2"
    ns vtep1 ip link set u1 mtu 1280; ns vtep2 ip link set u2 mtu 1280
    ns vtep1 ip addr add 192.168.211.1/24 dev u1
    ns vtep2 ip addr add 192.168.211.2/24 dev u2
    ns vtep1 ip link set u1 up; ns vtep2 ip link set u2 up
    # The daemon receives on a raw socket and verifies the UDP checksum in
    # software. veth enables TX checksum offload by default, so locally
    # generated UDP packets cross it with a partial checksum (CHECKSUM_PARTIAL)
    # that the kernel UDP stack accepts but the daemon would drop as malformed.
    # Physical NICs fill the checksum in hardware, so only this test needs it.
    ns vtep1 ethtool -K u1 tx off >/dev/null
    ns vtep2 ethtool -K u2 tx off >/dev/null
    # access
    local i
    for i in 1 2; do
        ip link add "a$i" type veth peer name "h$i"
        ip link set "a$i" netns "${P}vtep$i"; ip link set "h$i" netns "${P}host$i"
        ns "vtep$i" ip link set "a$i" mtu 1500; ns "host$i" ip link set "h$i" mtu 1500
        ns "vtep$i" ip link add br0 type bridge
        ns "vtep$i" ip link set "a$i" master br0
        ns "vtep$i" ip link set br0 up; ns "vtep$i" ip link set "a$i" up
        ns "host$i" ip addr add "10.200.0.$i/24" dev "h$i"
        ns "host$i" ip link set "h$i" up
    done
}

# ---------- daemons ----------
write_config() { # vtep_number local_ip peer_ip
    cat > "$TMP/vtep$1.conf" <<EOC
[global]
max_entries = 4096
max_bytes = 16777216
orphan_budget = 1024
[association lab]
local_ip = $2
peer_ip = $3
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
receiver = $MODE
S = 1000000
B = 10000000
I = 10000000
Delta = 20000000
R = 100000000
Q = 200000000
max_entries = 1024
max_bytes = 4194304
orphan_budget = 256
dtag_start = 0
dtag_rate = 1000000
dtag_burst = 10000
counters_file = $TMP/vtep$1.counters.$MODE
counters_interval = 200000000
EOC
}

has_schc0() { ns "vtep$1" ip link show schc0 >/dev/null 2>&1; }

start_daemon() { # vtep_number
    local n=$1
    ip netns exec "${P}vtep$n" "$BIN" -c "$TMP/vtep$n.conf" >>"$TMP/vtep$n.$MODE.log" 2>&1 &
    PIDS[$n]=$!
    if ! wait_for 10 has_schc0 "$n"; then
        fail "vtep$n: schc0 did not appear (see $TMP/vtep$n.$MODE.log)"
        return 1
    fi
    kill -0 "${PIDS[$n]}" 2>/dev/null || { fail "vtep$n: daemon died"; return 1; }
    ns "vtep$n" ip link set schc0 master br0
    ns "vtep$n" ip link set schc0 up
}

stop_daemon() { # vtep_number
    local n=$1
    [[ -n ${PIDS[$n]:-} ]] || return 0
    kill "${PIDS[$n]}" 2>/dev/null || true
    wait "${PIDS[$n]}" 2>/dev/null || true
    unset "PIDS[$n]"
    wait_for 5 bash -c "! ip netns exec ${P}vtep$n ip link show schc0 >/dev/null 2>&1" || true
}

start_both() {
    write_config 1 192.168.211.1 192.168.211.2
    write_config 2 192.168.211.2 192.168.211.1
    start_daemon 1 && start_daemon 2
    sleep 0.3   # quarantine Q
}

stop_both() { stop_daemon 1; stop_daemon 2; }

# ---------- measurement ----------
snapshot_counters() {
    local n
    for n in 1 2; do kill -USR1 "${PIDS[$n]}" 2>/dev/null || true; done
    sleep 0.5
}

counter() { # vtep_number name
    awk -v n="$2" '$1 == n { s += $2 } END { print s + 0 }' \
        "$TMP/vtep$1.counters.$MODE" 2>/dev/null || echo 0
}

counter_sum() { echo $(( $(counter 1 "$1") + $(counter 2 "$1") )); }

LOST=0
run_ping() { # count size  -> sets LOST
    local out sent recv
    out=$(ns host1 ping -c "$1" -i 0.2 -W 2 -M "do" -s "$2" 10.200.0.2 2>&1 || true)
    sent=$(sed -n 's/^\([0-9]*\) packets transmitted.*/\1/p' <<<"$out")
    recv=$(sed -n 's/.* \([0-9]*\) received.*/\1/p' <<<"$out")
    if [[ -z $sent || -z $recv ]]; then LOST=$1; echo "$out" >>"$TMP/ping-errors.log"; return; fi
    LOST=$((sent - recv)); (( LOST < 0 )) && LOST=0
    return 0
}

# ---------- steps ----------
step_pings() {
    local n
    for n in 56 1201 1202 1472; do
        run_ping 10 "$n"
        if (( LOST == 0 )); then pass "step1 ping -s $n: no loss"
        else fail "step1 ping -s $n: lost $LOST of 10"; fi
    done
}

step_counters() {
    snapshot_counters
    local u f i m
    u=$(counter_sum packets_sent_unfragmented); f=$(counter_sum packets_sent_fragmented)
    i=$(counter_sum integrity_failures); m=$(counter_sum malformed)
    (( u > 0 )) && pass "step2 packets_sent_unfragmented=$u" || fail "step2 packets_sent_unfragmented=0"
    (( f > 0 )) && pass "step2 packets_sent_fragmented=$f" || fail "step2 packets_sent_fragmented=0"
    (( i == 0 )) && pass "step2 integrity_failures=0" || fail "step2 integrity_failures=$i"
    (( m == 0 )) && pass "step2 malformed=0" || fail "step2 malformed=$m"
}

iperf_once() { # label [-R]
    ns host2 iperf3 -s -1 >"$TMP/iperf-server.log" 2>&1 &
    IPERF_PID=$!
    sleep 0.5
    if ns host1 timeout 30 iperf3 -c 10.200.0.2 -t 5 "${@:2}" >"$TMP/iperf-$MODE-$1.log" 2>&1; then
        pass "step3 iperf3 $1 completed"
    else
        fail "step3 iperf3 $1 failed (see $TMP/iperf-$MODE-$1.log)"
    fi
    kill "$IPERF_PID" 2>/dev/null || true
    wait "$IPERF_PID" 2>/dev/null || true
    IPERF_PID=""
}

step_iperf() {
    if ! command -v iperf3 >/dev/null; then info "step3 iperf3 not on PATH, skipped"; return; fi
    iperf_once forward
    iperf_once reverse -R
}

netem_on() { ns vtep1 tc qdisc add dev "$UNDERLAY_IF" root netem "$@"; }
netem_off() { ns vtep1 tc qdisc del dev "$UNDERLAY_IF" root 2>/dev/null || true; }

step_reorder() {
    netem_on delay 5ms reorder 50%
    run_ping 20 1472
    netem_off
    if [[ $MODE == tolerant ]]; then
        (( LOST <= 2 )) && pass "step4 reorder: lost $LOST of 20 (max 2)" || fail "step4 reorder: lost $LOST of 20 (max 2)"
    else
        info "step4 reorder (ordered receiver): lost $LOST of 20, loss expected"
    fi
}

step_loss() {
    netem_on loss 20%
    run_ping 30 1472
    netem_off
    info "step5 loss 20%: lost $LOST of 30"
    snapshot_counters
    if [[ $MODE == tolerant ]]; then
        local i; i=$(counter_sum integrity_failures)
        (( i == 0 )) && pass "step5 integrity_failures=0 after loss" || fail "step5 integrity_failures=$i after loss"
    fi
}

step_duplicates() {
    [[ $MODE == tolerant ]] || return 0
    netem_on duplicate 50%
    run_ping 20 1472
    netem_off
    snapshot_counters
    local d; d=$(counter_sum duplicates)
    (( LOST <= 1 )) && pass "step6 duplicate: lost $LOST of 20 (max 1)" || fail "step6 duplicate: lost $LOST of 20 (max 1)"
    (( d > 0 )) && pass "step6 duplicates counter=$d" || fail "step6 duplicates counter=0"
}

step_restart() {
    stop_daemon 1
    start_daemon 1 || return 0
    sleep 0.3   # quarantine Q
    run_ping 10 1472
    (( LOST == 0 )) && pass "step7 restart: no loss after vtep1 restart" || fail "step7 restart: lost $LOST of 10"
}

run_mode() {
    MODE=$1
    info "starting daemons"
    start_both || { stop_both; return 0; }
    step_pings
    step_counters
    step_iperf
    step_reorder
    step_loss
    step_duplicates
    step_restart
    stop_both
}

# ---------- main ----------
build_topology
run_mode ordered
run_mode tolerant

echo
if (( FAILS == 0 )); then echo "SUMMARY: all required steps passed"
else echo "SUMMARY: $FAILS required step(s) FAILED"; fi
(( FAILS == 0 ))
