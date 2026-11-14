import math

import pytest

from schcvx.crc32c import crc32c
from schcvx.receiver import (
    ABORTED,
    DELIVERED,
    DUPLICATE_IGNORED,
    LATE,
    MALFORMED,
    PEER_REJECTED,
    PENDING,
    RETIRED_DROP,
    TABLE_FULL,
    Receiver,
    ReceiverConfig,
)
from schcvx.wire import RULE_ID_0, Fragment, encode

PEER_A = "peerA"
PEER_B = "peerB"
VNI = 0x010203
FRAME_ID = 0x1122334455667788


class FakeClock:
    def __init__(self, t: float = 0.0):
        self.t = t

    def __call__(self) -> float:
        return self.t

    def advance(self, dt: float) -> None:
        self.t += dt


def _fragments(
    frame: bytes,
    slice_len: int,
    *,
    vni: int = VNI,
    frame_id: int = FRAME_ID,
    rule_id: int = RULE_ID_0,
) -> list[bytes]:
    count = math.ceil(len(frame) / slice_len)
    rcs = crc32c(frame)
    out = []
    for index in range(count):
        is_final = index == count - 1
        start = index * slice_len
        end = len(frame) if is_final else start + slice_len
        frag = Fragment(
            rule_id=rule_id,
            vni=vni,
            frame_id=frame_id,
            index=index,
            count=count,
            payload=frame[start:end],
            rcs=rcs if is_final else None,
        )
        out.append(encode(frag))
    return out


def _config(**overrides) -> ReceiverConfig:
    base = {
        "delta": 10.0,
        "retire_l": 5.0,
        "max_entries_per_peer": 10,
        "max_bytes_per_peer": 1_000_000,
        "max_entries_global": 10,
        "max_bytes_global": 1_000_000,
        "max_count": 8,
        "max_frame_len": 10_000,
        "max_slice_len": 2000,
        "authorized": {(PEER_A, VNI): RULE_ID_0, (PEER_B, VNI): RULE_ID_0},
    }
    base.update(overrides)
    return ReceiverConfig(**base)


# --- Counters shape ----------------------------------------------------------


def test_counters_has_required_keys():
    r = Receiver(_config(), FakeClock())
    assert set(r.counters) == {
        "completed",
        "expired",
        "late",
        "duplicate",
        "integrity_failure",
        "malformed",
        "peer_admission_failure",
        "table_exhaustion",
    }


# --- Two peers, different slice lengths --------------------------------------


def test_two_peers_different_slice_lengths():
    r = Receiver(_config(), FakeClock())
    frame_a = b"\x01" * 250
    frame_b = b"\x02" * 400
    frags_a = _fragments(frame_a, 100)
    frags_b = _fragments(frame_b, 150)

    *_, last_a = (r.receive(PEER_A, d) for d in frags_a)
    *_, last_b = (r.receive(PEER_B, d) for d in frags_b)

    assert isinstance(last_a, DELIVERED)
    assert last_a.frame == frame_a
    assert isinstance(last_b, DELIVERED)
    assert last_b.frame == frame_b


# --- Arrival order ------------------------------------------------------------


@pytest.mark.parametrize("reverse", [False, True])
def test_arrival_order_n2(reverse):
    r = Receiver(_config(), FakeClock())
    frame = b"\x03" * 50
    frags = _fragments(frame, 30)
    assert len(frags) == 2
    order = list(reversed(frags)) if reverse else frags

    results = [r.receive(PEER_A, d) for d in order]

    assert isinstance(results[-1], DELIVERED)
    assert results[-1].frame == frame


@pytest.mark.parametrize("reverse", [False, True])
def test_arrival_order_n3(reverse):
    r = Receiver(_config(), FakeClock())
    frame = b"\x04" * 25
    frags = _fragments(frame, 10)
    assert len(frags) == 3
    order = list(reversed(frags)) if reverse else frags

    results = [r.receive(PEER_A, d) for d in order]

    assert isinstance(results[-1], DELIVERED)
    assert results[-1].frame == frame


# --- Single-fragment loss, no delivery ---------------------------------------


@pytest.mark.parametrize("n,missing", [(2, 0), (2, 1), (3, 0), (3, 1), (3, 2)])
def test_single_fragment_loss_no_delivery(n, missing):
    r = Receiver(_config(), FakeClock())
    slice_len = 10
    frame_len = slice_len * (n - 1) + 5
    frame = bytes(i % 256 for i in range(frame_len))
    frags = _fragments(frame, slice_len)
    assert len(frags) == n

    results = [r.receive(PEER_A, d) for i, d in enumerate(frags) if i != missing]

    assert all(not isinstance(res, DELIVERED) for res in results)
    assert r.counters["completed"] == 0


# --- Duplicates ----------------------------------------------------------------


def test_identical_duplicate_ignored_without_extending_expiry():
    clock = FakeClock()
    r = Receiver(_config(delta=10.0), clock)
    frame = b"\x05" * 25
    frags = _fragments(frame, 15)

    r.receive(PEER_A, frags[0])
    dup_result = r.receive(PEER_A, frags[0])
    assert isinstance(dup_result, DUPLICATE_IGNORED)
    assert r.counters["duplicate"] == 1

    clock.advance(9.9)  # still before expiry (0 + delta 10); the duplicate above
    final = r.receive(PEER_A, frags[1])  # must not have pushed the deadline out
    assert isinstance(final, DELIVERED)


def test_conflicting_duplicate_aborts():
    r = Receiver(_config(), FakeClock())
    frame = b"\x06" * 25
    frags = _fragments(frame, 15)

    r.receive(PEER_A, frags[0])
    conflicting = Fragment(
        rule_id=RULE_ID_0, vni=VNI, frame_id=FRAME_ID, index=0, count=2, payload=b"\xff" * 15, rcs=None
    )
    result = r.receive(PEER_A, encode(conflicting))

    assert isinstance(result, ABORTED)
    assert result.reason == "conflicting duplicate"
    assert r.counters["integrity_failure"] == 1

    after = r.receive(PEER_A, frags[1])
    assert isinstance(after, RETIRED_DROP)


# --- Inconsistent headers -----------------------------------------------------


def test_inconsistent_count_aborts():
    r = Receiver(_config(), FakeClock())
    frame = b"\x07" * 25
    frags = _fragments(frame, 15)  # count == 2

    r.receive(PEER_A, frags[0])
    mismatched = Fragment(
        rule_id=RULE_ID_0, vni=VNI, frame_id=FRAME_ID, index=1, count=3, payload=b"\x07" * 5, rcs=None
    )
    result = r.receive(PEER_A, encode(mismatched))

    assert isinstance(result, ABORTED)
    assert result.reason == "inconsistent count"
    assert r.counters["integrity_failure"] == 1


def test_nonfinal_unequal_length_aborts():
    r = Receiver(_config(), FakeClock())
    frame = b"\x08" * 25
    frags = _fragments(frame, 10)  # count == 3, nonfinal len 10

    r.receive(PEER_A, frags[0])
    short_nonfinal = Fragment(
        rule_id=RULE_ID_0, vni=VNI, frame_id=FRAME_ID, index=1, count=3, payload=b"\x08" * 9, rcs=None
    )
    result = r.receive(PEER_A, encode(short_nonfinal))

    assert isinstance(result, ABORTED)
    assert "nonfinal" in result.reason
    assert r.counters["integrity_failure"] == 1


def test_final_longer_than_nonfinal_aborts_nonfinal_first():
    r = Receiver(_config(), FakeClock())
    frame = b"\x09" * 25
    frags = _fragments(frame, 10)  # count == 3, nonfinal len 10, final len 5

    r.receive(PEER_A, frags[0])
    oversized_final = Fragment(
        rule_id=RULE_ID_0,
        vni=VNI,
        frame_id=FRAME_ID,
        index=2,
        count=3,
        payload=b"\x09" * 11,
        rcs=crc32c(frame),
    )
    result = r.receive(PEER_A, encode(oversized_final))

    assert isinstance(result, ABORTED)
    assert result.reason == "final slice longer than a nonfinal slice"
    assert r.counters["integrity_failure"] == 1


def test_final_longer_than_nonfinal_aborts_final_first():
    r = Receiver(_config(), FakeClock())
    frame = b"\x0a" * 25
    frags = _fragments(frame, 10)  # count == 3, final len 5

    r.receive(PEER_A, frags[2])  # final arrives first, sets final_len = 5
    short_nonfinal = Fragment(
        rule_id=RULE_ID_0, vni=VNI, frame_id=FRAME_ID, index=0, count=3, payload=b"\x0a" * 4, rcs=None
    )
    result = r.receive(PEER_A, encode(short_nonfinal))

    assert isinstance(result, ABORTED)
    assert result.reason == "final slice longer than a nonfinal slice"
    assert r.counters["integrity_failure"] == 1


def test_retained_bytes_over_max_frame_len_aborts():
    r = Receiver(_config(max_frame_len=20), FakeClock())
    frame = b"\x0b" * 25
    frags = _fragments(frame, 15)  # lens 15, 10; sum 25 > max_frame_len 20

    r.receive(PEER_A, frags[0])
    result = r.receive(PEER_A, frags[1])

    assert isinstance(result, ABORTED)
    assert result.reason == "retained bytes exceed max_frame_len"
    assert r.counters["integrity_failure"] == 1


# --- Bad RCS -------------------------------------------------------------------


def test_bad_rcs_aborts():
    r = Receiver(_config(), FakeClock())
    frame = b"\x0c" * 25
    frags = _fragments(frame, 15)
    bad_final = Fragment(
        rule_id=RULE_ID_0,
        vni=VNI,
        frame_id=FRAME_ID,
        index=1,
        count=2,
        payload=frame[15:],
        rcs=0xDEADBEEF,
    )

    r.receive(PEER_A, frags[0])
    result = r.receive(PEER_A, encode(bad_final))

    assert isinstance(result, ABORTED)
    assert result.reason == "rcs mismatch"
    assert r.counters["integrity_failure"] == 1
    assert r.counters["completed"] == 0


# --- Deadline equality and expiry ---------------------------------------------


def test_deadline_equality_is_expired():
    clock = FakeClock()
    r = Receiver(_config(delta=10.0), clock)
    frame = b"\x0d" * 25
    frags = _fragments(frame, 15)

    r.receive(PEER_A, frags[0])  # opens entry at t=0, expiry=10
    clock.advance(10.0)  # now == expiry exactly: must be expired, per "t >= expiry"
    result = r.receive(PEER_A, frags[1])

    assert isinstance(result, LATE)
    assert r.counters["expired"] == 1
    assert r.counters["late"] == 1
    assert r.counters["completed"] == 0


def test_no_delivery_after_expiry():
    clock = FakeClock()
    r = Receiver(_config(delta=5.0), clock)
    frame = b"\x0e" * 25
    frags = _fragments(frame, 15)

    r.receive(PEER_A, frags[0])
    clock.advance(5.1)
    result = r.receive(PEER_A, frags[1])  # would complete the bitmap, but arrives late

    assert isinstance(result, LATE)
    assert r.counters["completed"] == 0
    assert r.counters["expired"] == 1
    assert r.counters["late"] == 1


def test_sweep_expires_without_new_fragment():
    clock = FakeClock()
    r = Receiver(_config(delta=5.0, retire_l=5.0), clock)
    frags = _fragments(b"\x0f" * 25, 15)

    r.receive(PEER_A, frags[0])
    clock.advance(5.0)
    r.sweep()

    assert r.counters["expired"] == 1
    assert r.counters["late"] == 0

    result = r.receive(PEER_A, frags[1])
    assert isinstance(result, RETIRED_DROP)
    assert r.counters["late"] == 1


# --- Late arrival after retirement, and retirement-record expiry -------------


def test_late_arrival_after_retirement():
    r = Receiver(_config(retire_l=5.0), FakeClock())
    frags = _fragments(b"\x10" * 25, 15)

    r.receive(PEER_A, frags[0])
    completed = r.receive(PEER_A, frags[1])
    assert isinstance(completed, DELIVERED)

    late = r.receive(PEER_A, frags[0])
    assert isinstance(late, RETIRED_DROP)
    assert r.counters["late"] == 1


def test_retirement_record_expires_after_l():
    clock = FakeClock()
    r = Receiver(_config(retire_l=5.0), clock)
    frags = _fragments(b"\x11" * 25, 15)

    r.receive(PEER_A, frags[0])
    r.receive(PEER_A, frags[1])  # completes and retires at t=0; retire_until=5

    clock.advance(5.0)
    r.sweep()  # purges the retirement record, freeing its table slot

    reopened = r.receive(PEER_A, frags[0])
    assert isinstance(reopened, PENDING)
    completed_again = r.receive(PEER_A, frags[1])
    assert isinstance(completed_again, DELIVERED)
    assert r.counters["completed"] == 2


# --- Table exhaustion -----------------------------------------------------------


def test_table_exhaustion_per_peer_and_recovery():
    clock = FakeClock()
    cfg = _config(max_entries_per_peer=1, max_entries_global=10, retire_l=5.0)
    r = Receiver(cfg, clock)
    frags1 = _fragments(b"\x12" * 25, 15, frame_id=1)
    frags2 = _fragments(b"\x12" * 25, 15, frame_id=2)

    r.receive(PEER_A, frags1[0])  # consumes the one available slot
    result = r.receive(PEER_A, frags2[0])
    assert isinstance(result, TABLE_FULL)
    assert r.counters["table_exhaustion"] == 1

    r.receive(PEER_A, frags1[1])  # completes frame 1; slot stays reserved for retirement
    result2 = r.receive(PEER_A, frags2[0])
    assert isinstance(result2, TABLE_FULL)

    clock.advance(5.0)
    r.sweep()
    result3 = r.receive(PEER_A, frags2[0])
    assert isinstance(result3, PENDING)


def test_table_exhaustion_by_bytes_cap():
    cfg = _config(max_bytes_per_peer=10, max_bytes_global=10)
    r = Receiver(cfg, FakeClock())
    frags = _fragments(b"\x13" * 25, 15)  # first slice alone (15 bytes) exceeds the cap

    result = r.receive(PEER_A, frags[0])

    assert isinstance(result, TABLE_FULL)
    assert r.counters["table_exhaustion"] == 1


# --- Unauthorized peer/VNI and RuleID ------------------------------------------


def test_unauthorized_peer_rejected():
    r = Receiver(_config(), FakeClock())
    frags = _fragments(b"\x14" * 25, 15)

    result = r.receive("unknown-peer", frags[0])

    assert isinstance(result, PEER_REJECTED)
    assert r.counters["peer_admission_failure"] == 1


def test_unauthorized_vni_rejected():
    r = Receiver(_config(), FakeClock())
    frags = _fragments(b"\x15" * 25, 15, vni=0x999999)

    result = r.receive(PEER_A, frags[0])

    assert isinstance(result, PEER_REJECTED)
    assert r.counters["peer_admission_failure"] == 1


def test_wrong_rule_id_rejected():
    r = Receiver(_config(), FakeClock())
    frags = _fragments(b"\x16" * 25, 15, rule_id=1)  # configured RuleID for this peer is 0

    result = r.receive(PEER_A, frags[0])

    assert isinstance(result, PEER_REJECTED)
    assert r.counters["peer_admission_failure"] == 1


# --- Malformed input -------------------------------------------------------------


def test_zero_or_false_udp_checksum_is_malformed():
    r = Receiver(_config(), FakeClock())
    frags = _fragments(b"\x17" * 25, 15)

    result = r.receive(PEER_A, frags[0], udp_checksum_ok=False)

    assert isinstance(result, MALFORMED)
    assert r.counters["malformed"] == 1


def test_short_datagram_is_malformed():
    r = Receiver(_config(), FakeClock())

    result = r.receive(PEER_A, b"\x00" * 5)

    assert isinstance(result, MALFORMED)
    assert r.counters["malformed"] == 1


def test_count_over_max_count_is_malformed():
    r = Receiver(_config(max_count=2), FakeClock())
    frags = _fragments(b"\x18" * 30, 10)  # count == 3 > max_count 2

    result = r.receive(PEER_A, frags[0])

    assert isinstance(result, MALFORMED)
    assert r.counters["malformed"] == 1


def test_slice_over_max_slice_len_is_malformed():
    r = Receiver(_config(max_slice_len=5), FakeClock())
    frags = _fragments(b"\x19" * 30, 10)  # slice len 10 > max_slice_len 5

    result = r.receive(PEER_A, frags[0])

    assert isinstance(result, MALFORMED)
    assert r.counters["malformed"] == 1
