# Namespace integration test (WP10)

`run.sh` exercises `schcvxd` end to end on one Linux host using network
namespaces; no hardware is needed. It must run as root.

Topology: namespaces `vtep1` and `vtep2` joined by a veth underlay (MTU 1280,
192.168.211.1/24 and .2/24). Each VTEP has a bridge `br0` holding the daemon's
TAP `schc0` and a veth port (MTU 1500) to a host namespace (`host1` 10.200.0.1,
`host2` 10.200.0.2). Daemons use the lab profile of `interop/README.md`.

For each receiver mode (`ordered`, then `tolerant`, with fresh daemons) it runs:
pings of 56/1201/1202/1472 bytes with DF; counter checks (both send counters
non-zero, no integrity failures or malformed datagrams); iperf3 TCP both
directions if `iperf3` is on PATH; netem reorder, loss and (tolerant only)
duplicate on the vtep1 underlay; and a restart of the vtep1 daemon. It prints
one PASS/FAIL/INFO line per step and exits non-zero if a required step failed.
The script disables TX checksum offload on the underlay veth ends (`ethtool -K tx off`):
the daemon verifies UDP checksums in software on a raw socket, and veth offload would
otherwise deliver partial checksums that are counted as malformed.
Namespaces are removed and daemons killed on exit; logs, configs and counter
files stay in the printed temporary directory.

Run on NixOS with tools from nixpkgs (needs `ip`, `tc`, `ping`, `ethtool`, optionally `iperf3`):

    nix shell nixpkgs#iproute2 nixpkgs#iputils nixpkgs#ethtool nixpkgs#iperf3 -c sudo -E env "PATH=$PATH" bash impl-c/tests/integration/run.sh build-c/linux/schcvxd
