"""Reference receiver: reassembly state machine, per DESIGN.md "Receiver algorithm".

Single-threaded and deterministic: the caller injects a monotonic clock
(a zero-argument callable returning the current time) instead of the
receiver reading a real clock, so tests control time exactly.

Entries are keyed by (peer, VNI, Frame ID); RuleID is validated against the
value configured per (peer, VNI) association but is not part of the key,
since DESIGN.md ties RuleID to the peer association rather than to the
Frame ID namespace.
"""

from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from typing import Any

from .crc32c import crc32c
from .wire import Fragment, MalformedFragment, decode

_Key = tuple[Any, int, int]  # (peer, vni, frame_id)


@dataclass(frozen=True)
class ReceiverConfig:
    delta: float
    retire_l: float
    max_entries_per_peer: int
    max_bytes_per_peer: int
    max_entries_global: int
    max_bytes_global: int
    max_count: int
    max_frame_len: int
    max_slice_len: int
    authorized: Mapping[tuple[Any, int], int]  # (peer, vni) -> configured RuleID


# --- Result outcomes, one dataclass per outcome named in the package spec --


@dataclass(frozen=True)
class DELIVERED:
    frame: bytes


@dataclass(frozen=True)
class PENDING:
    pass


@dataclass(frozen=True)
class DUPLICATE_IGNORED:
    pass


@dataclass(frozen=True)
class RETIRED_DROP:
    pass


@dataclass(frozen=True)
class LATE:
    pass


@dataclass(frozen=True)
class ABORTED:
    reason: str


@dataclass(frozen=True)
class MALFORMED:
    pass


@dataclass(frozen=True)
class PEER_REJECTED:
    pass


@dataclass(frozen=True)
class TABLE_FULL:
    pass


Result = (
    DELIVERED
    | PENDING
    | DUPLICATE_IGNORED
    | RETIRED_DROP
    | LATE
    | ABORTED
    | MALFORMED
    | PEER_REJECTED
    | TABLE_FULL
)


@dataclass
class _Entry:
    peer: Any
    count: int
    expiry: float
    slices: dict[int, bytes] = field(default_factory=dict)
    nonfinal_len: int | None = None
    final_len: int | None = None
    total_bytes: int = 0
    rcs: int | None = None


class Receiver:
    def __init__(self, config: ReceiverConfig, clock: Callable[[], float]):
        self._config = config
        self._clock = clock
        self.counters: dict[str, int] = {
            "completed": 0,
            "expired": 0,
            "late": 0,
            "duplicate": 0,
            "integrity_failure": 0,
            "malformed": 0,
            "peer_admission_failure": 0,
            "table_exhaustion": 0,
        }
        self._entries: dict[_Key, _Entry] = {}
        self._retirements: dict[_Key, float] = {}
        self._peer_slots: dict[Any, int] = {}
        self._global_slots = 0
        self._bytes_peer: dict[Any, int] = {}
        self._bytes_global = 0

    def receive(self, peer: Any, datagram: bytes, udp_checksum_ok: bool = True) -> Result:
        now = self._clock()
        cfg = self._config

        if not udp_checksum_ok:
            self.counters["malformed"] += 1
            return MALFORMED()

        try:
            frag = decode(datagram)
        except MalformedFragment:
            self.counters["malformed"] += 1
            return MALFORMED()

        expected_rule_id = cfg.authorized.get((peer, frag.vni))
        if expected_rule_id is None or frag.rule_id != expected_rule_id:
            self.counters["peer_admission_failure"] += 1
            return PEER_REJECTED()

        if frag.count > cfg.max_count or len(frag.payload) > cfg.max_slice_len:
            self.counters["malformed"] += 1
            return MALFORMED()

        key: _Key = (peer, frag.vni, frag.frame_id)

        if key in self._retirements:
            self.counters["late"] += 1
            return RETIRED_DROP()

        entry = self._entries.get(key)
        if entry is None:
            admitted = self._admit_new_entry(peer, len(frag.payload))
            if admitted is not None:
                return admitted
            entry = _Entry(peer=peer, count=frag.count, expiry=now + cfg.delta)
            self._entries[key] = entry
            self._peer_slots[peer] = self._peer_slots.get(peer, 0) + 1
            self._global_slots += 1
        else:
            if now >= entry.expiry:
                self._retire(key, entry, now)
                self.counters["expired"] += 1
                self.counters["late"] += 1
                return LATE()
            if frag.count != entry.count:
                self._retire(key, entry, now)
                self.counters["integrity_failure"] += 1
                return ABORTED("inconsistent count")

        return self._apply_fragment(key, entry, frag, now)

    def sweep(self) -> None:
        now = self._clock()
        for key, entry in list(self._entries.items()):
            if now >= entry.expiry:
                self._retire(key, entry, now)
                self.counters["expired"] += 1
        for key, retire_until in list(self._retirements.items()):
            if now >= retire_until:
                del self._retirements[key]
                peer = key[0]
                self._peer_slots[peer] -= 1
                self._global_slots -= 1

    def _admit_new_entry(self, peer: Any, slice_len: int) -> Result | None:
        cfg = self._config
        if (
            self._global_slots >= cfg.max_entries_global
            or self._peer_slots.get(peer, 0) >= cfg.max_entries_per_peer
        ):
            self.counters["table_exhaustion"] += 1
            return TABLE_FULL()
        if (
            self._bytes_peer.get(peer, 0) + slice_len > cfg.max_bytes_per_peer
            or self._bytes_global + slice_len > cfg.max_bytes_global
        ):
            self.counters["table_exhaustion"] += 1
            return TABLE_FULL()
        return None

    def _apply_fragment(self, key: _Key, entry: _Entry, frag: Fragment, now: float) -> Result:
        cfg = self._config
        peer = entry.peer
        is_final = frag.index == frag.count - 1
        slice_len = len(frag.payload)

        existing = entry.slices.get(frag.index)
        if existing is not None:
            if existing == frag.payload:
                self.counters["duplicate"] += 1
                return DUPLICATE_IGNORED()
            self._retire(key, entry, now)
            self.counters["integrity_failure"] += 1
            return ABORTED("conflicting duplicate")

        if is_final:
            if entry.nonfinal_len is not None and slice_len > entry.nonfinal_len:
                self._retire(key, entry, now)
                self.counters["integrity_failure"] += 1
                return ABORTED("final slice longer than a nonfinal slice")
        else:
            if entry.nonfinal_len is None:
                entry.nonfinal_len = slice_len
            elif slice_len != entry.nonfinal_len:
                self._retire(key, entry, now)
                self.counters["integrity_failure"] += 1
                return ABORTED("inconsistent nonfinal slice length")
            if entry.final_len is not None and entry.final_len > slice_len:
                self._retire(key, entry, now)
                self.counters["integrity_failure"] += 1
                return ABORTED("final slice longer than a nonfinal slice")

        prospective_total = entry.total_bytes + slice_len
        if prospective_total > cfg.max_frame_len:
            self._retire(key, entry, now)
            self.counters["integrity_failure"] += 1
            return ABORTED("retained bytes exceed max_frame_len")

        prospective_peer_bytes = self._bytes_peer.get(peer, 0) + slice_len
        prospective_global_bytes = self._bytes_global + slice_len
        if (
            prospective_peer_bytes > cfg.max_bytes_per_peer
            or prospective_global_bytes > cfg.max_bytes_global
        ):
            self.counters["table_exhaustion"] += 1
            return TABLE_FULL()

        entry.slices[frag.index] = frag.payload
        entry.total_bytes = prospective_total
        self._bytes_peer[peer] = prospective_peer_bytes
        self._bytes_global = prospective_global_bytes
        if is_final:
            entry.final_len = slice_len
            entry.rcs = frag.rcs

        if len(entry.slices) < entry.count:
            return PENDING()

        total = entry.total_bytes
        if not 1 <= total <= cfg.max_frame_len:
            self._retire(key, entry, now)
            self.counters["integrity_failure"] += 1
            return ABORTED("invalid total length")

        assembled = b"".join(entry.slices[i] for i in range(entry.count))
        if crc32c(assembled) != entry.rcs:
            self._retire(key, entry, now)
            self.counters["integrity_failure"] += 1
            return ABORTED("rcs mismatch")

        self._retire(key, entry, now)
        self.counters["completed"] += 1
        return DELIVERED(assembled)

    def _retire(self, key: _Key, entry: _Entry, now: float) -> None:
        self._bytes_peer[entry.peer] = self._bytes_peer.get(entry.peer, 0) - entry.total_bytes
        self._bytes_global -= entry.total_bytes
        del self._entries[key]
        self._retirements[key] = now + self._config.retire_l
