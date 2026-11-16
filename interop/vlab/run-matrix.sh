#!/usr/bin/env bash
# Virtual SCHC-VXLAN interop lab: build images, deploy with Containerlab, run the test matrix for
# both receiver modes, write results to interop/vlab/out/<timestamp>/ and print a summary.
# Usage: interop/vlab/run-matrix.sh [-f|--force-build] [--modes "ordered tolerant"] [--iperf-secs N]
# Requires: Docker (Docker Desktop is fine). Containerlab runs from its official container image.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
CLAB_IMAGE=${CLAB_IMAGE:-ghcr.io/srl-labs/clab:latest}
RUST_REF=${RUST_REF:-origin/main}
MODES="ordered tolerant"
IPERF_SECS=10
FORCE_BUILD=0
while [[ $# -gt 0 ]]; do
    case $1 in
        -f|--force-build) FORCE_BUILD=1 ;;
        --modes) MODES=$2; shift ;;
        --iperf-secs) IPERF_SECS=$2; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

TS=$(date +%Y%m%d-%H%M%S)
OUT=$HERE/out/$TS
STAGE=$HERE/out/.stage            # build contexts and the staged topology (git-ignored)
LAB=schcvx
N() { echo "clab-$LAB-$1"; }       # container name of node $1
mkdir -p "$OUT" "$STAGE"

FAILS=0
MODE=""
pass() { echo "PASS [$MODE] $*"; }
fail() { echo "FAIL [$MODE] $*"; FAILS=$((FAILS + 1)); }
info() { echo "INFO [$MODE] $*"; }
SUMMARY=$OUT/summary.tsv
printf 'step\tmode\tfwd_mbps\trev_mbps\tretrans\tc_integrity_failures\tc_expired\traw_drops\tcpu_c_s\tcpu_rust_s\n' >"$SUMMARY"

clab() {
    docker run --rm --privileged --network host --pid host \
        -e RECEIVER="${RECEIVER:-ordered}" \
        -v /var/run/docker.sock:/var/run/docker.sock \
        -v /var/run/netns:/var/run/netns \
        -v /var/lib/docker/containers:/var/lib/docker/containers \
        -v /etc/hosts:/etc/hosts \
        -v "$STAGE:$STAGE" -w "$STAGE" \
        "$CLAB_IMAGE" containerlab "$@"
}

destroy_lab() {
    clab destroy -t "$STAGE/schcvx.clab.yml" --cleanup >/dev/null 2>&1 || true
    docker network rm schcvx-mgmt >/dev/null 2>&1 || true   # clab leaves a custom mgmt network behind
}
cleanup() {
    local rc=$?
    set +e
    echo "INFO cleaning up"
    destroy_lab
    docker rm -f "$(N hostA)" "$(N hostB)" "$(N vtep1)" "$(N vtep2)" >/dev/null 2>&1
    rm -rf "$STAGE"
    exit $rc
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# ---------- images ----------
build_images() {
    local need=0 i
    for i in c rust host; do
        if [[ $FORCE_BUILD == 1 ]] || ! docker image inspect "schcvx-lab-$i:latest" >/dev/null 2>&1; then need=1; fi
    done
    [[ $need == 1 ]] || { echo "INFO images present (use --force-build to rebuild)"; return 0; }

    if [[ $FORCE_BUILD == 1 ]] || ! docker image inspect schcvx-lab-c:latest >/dev/null 2>&1; then
        echo "INFO building C image (runs meson test)"
        rm -rf "$STAGE/ctx-c"; mkdir -p "$STAGE/ctx-c/draft"
        cp -R "$REPO/impl-c" "$STAGE/ctx-c/impl-c"
        rm -rf "$STAGE/ctx-c/impl-c/build"* 2>/dev/null || true
        cp -R "$REPO/draft/revised" "$STAGE/ctx-c/draft/revised"
        cp "$HERE/images/c/entrypoint.sh" "$STAGE/ctx-c/entrypoint.sh"
        docker build -f "$HERE/images/c/Dockerfile" -t schcvx-lab-c:latest "$STAGE/ctx-c"
    fi
    if [[ $FORCE_BUILD == 1 ]] || ! docker image inspect schcvx-lab-rust:latest >/dev/null 2>&1; then
        echo "INFO building Rust image from $RUST_REF:interop/impl-rust"
        rm -rf "$STAGE/ctx-rust"; mkdir -p "$STAGE/ctx-rust"
        git -C "$REPO" archive "$RUST_REF" interop/impl-rust | tar -x -C "$STAGE/ctx-rust"
        cp "$HERE/images/rust/entrypoint.sh" "$STAGE/ctx-rust/entrypoint.sh"
        docker build -f "$HERE/images/rust/Dockerfile" -t schcvx-lab-rust:latest "$STAGE/ctx-rust"
    fi
    if [[ $FORCE_BUILD == 1 ]] || ! docker image inspect schcvx-lab-host:latest >/dev/null 2>&1; then
        echo "INFO building host image"
        docker build -f "$HERE/images/host/Dockerfile" -t schcvx-lab-host:latest "$HERE/images/host"
    fi
    rm -rf "$STAGE/ctx-c" "$STAGE/ctx-rust"
}

# ---------- helpers ----------
dx() { local n=$1; shift; docker exec "$(N "$n")" "$@"; }

wait_ready() {
    local i
    for ((i = 0; i < 150; i++)); do
        if dx vtep1 sh -c 'ip link show schc0 2>/dev/null | grep -q "master br0" && pgrep -x schcvxd >/dev/null' 2>/dev/null \
           && dx vtep2 sh -c 'pgrep -x vtep >/dev/null' 2>/dev/null \
           && dx hostA ip -4 addr show eth1 2>/dev/null | grep -q 192.168.44.2 \
           && dx hostB ip -4 addr show eth1 2>/dev/null | grep -q 192.168.44.3; then
            pass "daemons up: vtep1 schc0 bridged into br0, vtep2 vtep running"
            sleep 1   # startup quarantine Q = 200 ms plus margin
            return 0
        fi
        sleep 0.2
    done
    fail "lab not ready after 30 s"
    return 1
}

cpu_ticks() { # node process
    dx "$1" sh -c "awk '{print \$14+\$15}' /proc/\$(pgrep -x $2 | head -1)/stat" 2>/dev/null || echo 0
}
raw_drops() { dx "$1" sh -c "awk 'NR>1{s+=\$NF}END{print s+0}' /proc/net/raw" 2>/dev/null || echo 0; }
c_counter() { # name -> value from the last written counters file
    dx vtep1 awk -v n="$1" '$1==n{s+=$2}END{print s+0}' /run/schcvx/counters 2>/dev/null || echo 0
}

SD=""        # per-step output directory
CPU1_0=0; CPU2_0=0
step_begin() { # name
    SD=$OUT/$MODE/$1; mkdir -p "$SD"
    CPU1_0=$(cpu_ticks vtep1 schcvxd); CPU2_0=$(cpu_ticks vtep2 vtep)
    dx vtep1 rm -f /tmp/cap.pcap
    docker exec -d "$(N vtep1)" sh -c 'tcpdump -Z root -U -s 0 -i eth2 -c 3000 -w /tmp/cap.pcap udp port 60789 2>/dev/null'
    sleep 0.5
}
# step_end name fwd rev retrans
step_end() {
    local name=$1 fwd=${2:--} rev=${3:--} rt=${4:--}
    dx vtep1 pkill -INT tcpdump 2>/dev/null || true
    sleep 0.5
    docker cp "$(N vtep1):/tmp/cap.pcap" "$SD/vtep1-eth2.pcap" >/dev/null 2>&1 || true
    dx vtep1 sh -c 'kill -USR1 $(pgrep -x schcvxd)' || true
    sleep 0.6
    dx vtep1 cat /run/schcvx/counters >"$SD/c-counters.txt" 2>/dev/null || true
    dx vtep2 tail -n 40 /var/log/vtep.log >"$SD/rust-log-tail.txt" 2>/dev/null || true
    dx vtep1 cat /proc/net/raw >"$SD/vtep1-proc-net-raw.txt" 2>/dev/null || true
    dx vtep2 cat /proc/net/raw >"$SD/vtep2-proc-net-raw.txt" 2>/dev/null || true
    dx vtep2 cat /proc/net/udp >"$SD/vtep2-proc-net-udp.txt" 2>/dev/null || true
    local c1 c2 ci ce rd
    c1=$(( $(cpu_ticks vtep1 schcvxd) - CPU1_0 )); c2=$(( $(cpu_ticks vtep2 vtep) - CPU2_0 ))
    ci=$(c_counter integrity_failures); ce=$(c_counter expired)
    rd=$(( $(raw_drops vtep1) + $(raw_drops vtep2) ))
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$MODE" "$fwd" "$rev" "$rt" "$ci" "$ce" "$rd" \
        "$(awk -v t="$c1" 'BEGIN{printf "%.2f", t/100}')" "$(awk -v t="$c2" 'BEGIN{printf "%.2f", t/100}')" >>"$SUMMARY"
    info "$name: cpu C=${c1} Rust=${c2} ticks, integrity_failures=$ci expired=$ce raw_drops=$rd"
}

LOST=0
ping_step() { # size
    local n=ping-$1 out
    step_begin "$n"
    out=$(dx hostA ping -c 10 -i 0.2 -W 2 -M do -s "$1" 192.168.44.3 2>&1 || true)
    echo "$out" >"$SD/ping.txt"
    LOST=$(sed -n 's/.* \([0-9]*\)% packet loss.*/\1/p' <<<"$out")
    LOST=${LOST:-100}
    if [[ $LOST == 0 ]]; then pass "$n: 0% loss"; else fail "$n: ${LOST}% loss"; fi
    step_end "$n"
}

IPERF_MBPS=-; IPERF_RETR=-
iperf_run() { # label extra-args...   -> sets IPERF_MBPS, IPERF_RETR; returns 0 on success
    local label=$1; shift
    local js=$SD/iperf-$label.json
    dx hostA timeout $((IPERF_SECS + 30)) iperf3 -c 192.168.44.3 -t "$IPERF_SECS" --json "$@" >"$js" 2>"$js.err" || true
    IPERF_MBPS=$(docker exec -i "$(N hostA)" jq -r '(.end.sum_received.bits_per_second // empty) / 1e6 | . * 100 | round / 100' <"$js" 2>/dev/null || true)
    IPERF_RETR=$(docker exec -i "$(N hostA)" jq -r '.end.sum_sent.retransmits // 0' <"$js" 2>/dev/null || true)
    [[ -n $IPERF_MBPS ]] || { IPERF_MBPS=-; IPERF_RETR=-; return 1; }
}

iperf_step() { # name [extra args]   forward then reverse
    local name=$1; shift
    local f r rt1 rt2 ok=1
    step_begin "$name"
    iperf_run fwd "$@" || ok=0; f=$IPERF_MBPS; rt1=$IPERF_RETR
    iperf_run rev -R "$@" || ok=0; r=$IPERF_MBPS; rt2=$IPERF_RETR
    local rt="$rt1/$rt2"
    if [[ $ok == 1 ]]; then pass "$name: fwd ${f} Mbit/s rev ${r} Mbit/s retrans ${rt}"
    else fail "$name: iperf3 did not complete (fwd=$f rev=$r), see $SD"; fi
    step_end "$name" "$f" "$r" "$rt"
}

netem_step() { # name netem-args... ; forward direction C->Rust passes through the impaired egress
    local name=$1; shift
    local extra=()
    if [[ ${1:-} == --iperf-args ]]; then extra=($2); shift 2; fi
    dx vtep1 tc qdisc add dev eth2 root netem "$@" || { fail "$name: tc netem failed"; return 0; }
    dx vtep1 tc qdisc show dev eth2 >"$OUT/$MODE-$name-qdisc.txt" 2>/dev/null || true
    step_begin "$name"
    local f=- r=- rt=- ok=1
    iperf_run fwd ${extra[@]+"${extra[@]}"} || ok=0; f=$IPERF_MBPS; rt=$IPERF_RETR
    if [[ $ok == 1 ]]; then info "$name: netem $*: fwd ${f} Mbit/s retrans ${rt}"
    else info "$name: netem $*: iperf3 produced no result (expected possible under reorder)"; fi
    step_end "$name" "$f" "-" "$rt"
    dx vtep1 tc qdisc del dev eth2 root 2>/dev/null || true
}

run_mode() {
    MODE=$1
    export RECEIVER=$MODE
    info "deploying lab (RECEIVER=$MODE)"
    clab deploy -t "$STAGE/schcvx.clab.yml" --reconfigure >"$OUT/deploy-$MODE.log" 2>&1 \
        || { fail "containerlab deploy failed, see $OUT/deploy-$MODE.log"; tail -20 "$OUT/deploy-$MODE.log"; return 0; }
    wait_ready || { dx vtep1 sh -c 'true' && docker logs "$(N vtep2)" 2>&1 | tail -20; return 0; }
    docker exec -d "$(N hostB)" iperf3 -s
    sleep 0.5

    local s
    for s in 56 1201 1202 1472; do ping_step "$s"; done
    iperf_step iperf
    netem_step netem-a delay 5ms reorder 50%
    netem_step netem-b delay 5ms reorder 50% limit 100000
    netem_step netem-c-mss1100 --iperf-args "-M 1100" delay 5ms reorder 50% limit 100000
    docker logs "$(N vtep1)" >"$OUT/$MODE/vtep1-docker.log" 2>&1 || true
    docker logs "$(N vtep2)" >"$OUT/$MODE/vtep2-docker.log" 2>&1 || true
    destroy_lab
}

# ---------- main ----------
command -v docker >/dev/null || { echo "docker not found" >&2; exit 2; }
build_images
docker image inspect "$CLAB_IMAGE" >/dev/null 2>&1 || docker pull "$CLAB_IMAGE"
cp "$HERE/schcvx.clab.yml" "$STAGE/schcvx.clab.yml"

for m in $MODES; do run_mode "$m"; done

echo
echo "================ SUMMARY ($OUT) ================"
column -t -s "$(printf '\t')" "$SUMMARY" 2>/dev/null || cat "$SUMMARY"
echo "(fwd/rev = iperf3 TCP Mbit/s; retrans = fwd/rev; counters are C-side, cumulative per deployment;"
echo " raw_drops = /proc/net/raw drops summed over both VTEPs; cpu in seconds per step)"
echo
if (( FAILS == 0 )); then echo "RESULT: no FAIL lines"; else echo "RESULT: $FAILS FAIL line(s)"; fi
(( FAILS == 0 ))
