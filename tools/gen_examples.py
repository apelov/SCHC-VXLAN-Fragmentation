"""Generates draft/generated/examples.md from the reference sender and receiver.

Both worked examples DESIGN.md "Items to settle" #5 asks for are computed here,
not hand-written: a last-fragment-first completion (Example A) and a deadline
expiry followed by a late arrival (Example B). Regenerate with:

    python3 tools/gen_examples.py

`tests/test_examples.py` checks that regenerating reproduces the committed
file byte for byte, and that the printed counters match the narrative.
"""

from __future__ import annotations

import pathlib
import sys

_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_ROOT / "src"))

from schcvx.crc32c import crc32c
from schcvx.receiver import DELIVERED, LATE, PENDING, Receiver, ReceiverConfig
from schcvx.sender import fragment_frame
from schcvx.wire import RULE_ID_0, decode

OUTPUT_PATH = _ROOT / "draft" / "generated" / "examples.md"

PEER = "vtep-2"
VNI = 0x003039  # illustrative VNI 12345
FRAME_ID = 1
PMTU = 1500
H_IP = 20  # IPv4, no outer IP options
MAX_FRAME_LEN = 1514
MAX_FRAGMENTS = 2
DELTA = 5.0  # ms, illustrative deadline
RETIRE_L = 50.0  # ms, illustrative retirement-record lifetime

# Deterministic illustrative 1514-byte Ethernet frame: byte i = i mod 256.
FRAME = bytes(i % 256 for i in range(1514))


def _config() -> ReceiverConfig:
    return ReceiverConfig(
        delta=DELTA,
        retire_l=RETIRE_L,
        max_entries_per_peer=10,
        max_bytes_per_peer=1_000_000,
        max_entries_global=10,
        max_bytes_global=1_000_000,
        max_count=MAX_FRAGMENTS,
        max_frame_len=MAX_FRAME_LEN,
        max_slice_len=MAX_FRAME_LEN,
        authorized={(PEER, VNI): RULE_ID_0},
    )


class _FixedClock:
    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t

    def set(self, t: float) -> None:
        self.t = t


def _datagrams() -> list[bytes]:
    datagrams = fragment_frame(
        FRAME,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=PMTU,
        h_ip=H_IP,
        max_frame_len=MAX_FRAME_LEN,
        max_fragments=MAX_FRAGMENTS,
    )
    assert isinstance(datagrams, list)
    return sorted(datagrams, key=lambda d: decode(d).index)


def _outer_ip_len(datagram: bytes) -> int:
    return H_IP + 8 + len(datagram)


def _bitmap(received: set[int], count: int) -> str:
    return "".join("1" if i in received else "0" for i in range(count))


def build_example_a() -> dict:
    datagram0, datagram1 = _datagrams()
    frag0, frag1 = decode(datagram0), decode(datagram1)
    rcs = crc32c(FRAME)

    clock = _FixedClock()
    r = Receiver(_config(), clock)
    events = []
    received: set[int] = set()

    clock.set(0.0)
    result_final = r.receive(PEER, datagram1)
    received.add(frag1.index)
    events.append(
        {
            "t": "0.0 ms",
            "label": "final fragment (index 1) arrives first",
            "datagram": datagram1,
            "result": type(result_final).__name__,
            "bitmap": _bitmap(received, 2),
        }
    )

    clock.set(1.0)
    result_nonfinal = r.receive(PEER, datagram0)
    received.add(frag0.index)
    events.append(
        {
            "t": "1.0 ms",
            "label": "nonfinal fragment (index 0) arrives second, before expiry at 5.0 ms",
            "datagram": datagram0,
            "result": type(result_nonfinal).__name__,
            "bitmap": _bitmap(received, 2),
        }
    )

    assert isinstance(result_final, PENDING)
    assert isinstance(result_nonfinal, DELIVERED)
    assert result_nonfinal.frame == FRAME
    assert crc32c(result_nonfinal.frame) == rcs

    return {
        "datagram0": datagram0,
        "datagram1": datagram1,
        "frag0": frag0,
        "frag1": frag1,
        "rcs": rcs,
        "events": events,
        "counters": dict(r.counters),
    }


def build_example_b() -> dict:
    datagram0, datagram1 = _datagrams()
    frag0, frag1 = decode(datagram0), decode(datagram1)

    clock = _FixedClock()
    r = Receiver(_config(), clock)
    events = []
    received: set[int] = set()

    clock.set(0.0)
    result_final = r.receive(PEER, datagram1)
    received.add(frag1.index)
    events.append(
        {
            "t": "0.0 ms",
            "label": "final fragment (index 1) arrives alone; entry opens, expiry = 0.0 + Δ = 5.0 ms",
            "datagram": datagram1,
            "result": type(result_final).__name__,
            "bitmap": _bitmap(received, 2),
        }
    )

    late_t = DELTA + 2.0
    clock.set(late_t)
    result_late = r.receive(PEER, datagram0)
    events.append(
        {
            "t": f"{late_t:.1f} ms",
            "label": (
                "missing nonfinal fragment (index 0) arrives after expiry; "
                "the entry is retired as expired and the fragment is counted as late"
            ),
            "datagram": datagram0,
            "result": type(result_late).__name__,
            "bitmap": _bitmap(received, 2),
        }
    )

    assert isinstance(result_final, PENDING)
    assert isinstance(result_late, LATE)

    return {
        "datagram0": datagram0,
        "datagram1": datagram1,
        "frag0": frag0,
        "frag1": frag1,
        "events": events,
        "counters": dict(r.counters),
        "expiry_ms": DELTA,
    }


def _header_fields_hex(datagram: bytes) -> str:
    rule_id, vni, frame_id, index, count = (
        datagram[0:1],
        datagram[1:4],
        datagram[4:12],
        datagram[12:13],
        datagram[13:14],
    )
    return (
        f"RuleID=0x{rule_id.hex()} VNI=0x{vni.hex()} FrameID=0x{frame_id.hex()} "
        f"Index=0x{index.hex()} Count=0x{count.hex()}"
    )


def _rcs_hex(datagram: bytes) -> str:
    return datagram[-4:].hex()


def _fragment_row(datagram: bytes, rcs_column: str) -> str:
    frag = decode(datagram)
    cells = [
        str(frag.index),
        str(frag.count),
        str(len(frag.payload)),
        str(_outer_ip_len(datagram)),
        _header_fields_hex(datagram),
        rcs_column,
    ]
    return "| " + " | ".join(cells) + " |"


def _fragment_table(datagram0: bytes, datagram1: bytes) -> str:
    lines = [
        "| Index | Count | Slice length | Outer IP length | Header fields (hex) | RCS (hex) |",
        "| --- | --- | --- | --- | --- | --- |",
        _fragment_row(datagram0, "—"),
        _fragment_row(datagram1, _rcs_hex(datagram1)),
    ]
    return "\n".join(lines)


def _event_table(events: list[dict]) -> str:
    lines = [
        "| t | Event | Header fields (hex) | Result | Bitmap after datagram |",
        "| --- | --- | --- | --- | --- |",
    ]
    for ev in events:
        cells = [
            ev["t"],
            ev["label"],
            _header_fields_hex(ev["datagram"]),
            ev["result"],
            ev["bitmap"],
        ]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def _counters_line(counters: dict) -> str:
    return ", ".join(f"{key}={value}" for key, value in counters.items())


def render(example_a: dict, example_b: dict) -> str:
    rcs_hex = format(example_a["rcs"], "08x")
    lines = [
        "# Worked examples (generated)",
        "",
        "This file is generated by `tools/gen_examples.py` from the reference model",
        "in `src/schcvx/sender.py` and `src/schcvx/receiver.py`. Do not edit by hand;",
        "regenerate with `python3 tools/gen_examples.py`.",
        "",
        "## Common parameters",
        "",
        "- RuleID: 0 (prototype value)",
        f"- VNI: {VNI} (0x{VNI:06x})",
        f"- Frame ID: {FRAME_ID} (0x{FRAME_ID:016x})",
        f"- Peer: {PEER}",
        f"- PMTU: {PMTU} bytes, IPv4, H_IP = {H_IP} bytes (no outer IP options)",
        "- Illustrative Ethernet frame: 1514 bytes, deterministic pattern byte[i] = i mod 256",
        f"- Δ (deadline): {DELTA:.1f} ms; L (retirement record lifetime): {RETIRE_L:.1f} ms",
        "",
        "### Fragments produced by the sender",
        "",
        _fragment_table(example_a["datagram0"], example_a["datagram1"]),
        "",
        f"CRC-32C over the 1514-byte frame: 0x{rcs_hex}",
        "",
        "## Example A — last-fragment-first completion",
        "",
        "The final fragment (index 1, carrying the RCS) arrives before the nonfinal",
        "fragment (index 0). The receiver opens the entry on the final fragment's",
        "arrival and delivers once the bitmap is complete and the RCS verifies.",
        "",
        _event_table(example_a["events"]),
        "",
        f"Delivered frame length: {len(FRAME)} bytes; CRC-32C: 0x{rcs_hex}.",
        f"Counters after Example A: {_counters_line(example_a['counters'])}.",
        "",
        "## Example B — deadline expiry",
        "",
        "Only the final fragment (index 1) arrives. The entry opens with",
        f"expiry = first_rx + Δ = 0.0 + {DELTA:.1f} = {example_b['expiry_ms']:.1f} ms. No sweep",
        "runs. The missing nonfinal fragment (index 0) then arrives after expiry: the",
        "entry is ineligible for completion even though no sweep has retired it yet,",
        "so it is retired as expired on this arrival and the arriving fragment is",
        "itself counted as late.",
        "",
        _event_table(example_b["events"]),
        "",
        f"Counters after Example B: {_counters_line(example_b['counters'])}.",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    example_a = build_example_a()
    example_b = build_example_b()
    text = render(example_a, example_b)
    print(text)
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(text)


if __name__ == "__main__":
    main()
