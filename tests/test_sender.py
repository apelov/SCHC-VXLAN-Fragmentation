import inspect

import pytest

from schcvx.crc32c import crc32c
from schcvx.sender import Drop, Fits, fragment_frame, slice_budget, vxlan_frame_budget
from schcvx.wire import HEADER_LEN, RCS_LEN, decode

VNI = 0x010203
FRAME_ID = 0x1122334455667788


def _outer_ip_len(datagram: bytes, h_ip: int) -> int:
    return h_ip + 8 + len(datagram)


def _reassemble(datagrams: list[bytes]) -> bytes:
    fragments = [decode(d) for d in datagrams]
    fragments.sort(key=lambda f: f.index)
    return b"".join(f.payload for f in fragments)


# --- DESIGN.md pinned numbers, PMTU 1500, no outer IP options ---------------


def test_vxlan_frame_budget_pinned_numbers():
    assert vxlan_frame_budget(1500, h_ip=20) == 1464  # IPv4
    assert vxlan_frame_budget(1500, h_ip=40) == 1444  # IPv6


def test_slice_budget_pinned_numbers():
    assert slice_budget(1500, h_ip=20) == 1454  # IPv4
    assert slice_budget(1500, h_ip=40) == 1434  # IPv6


def test_1514_byte_frame_ipv4_pinned_slices_and_ip_lengths():
    frame = bytes(i % 256 for i in range(1514))  # deterministic 1514 bytes
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=1514,
        max_fragments=2,
    )
    assert isinstance(result, list)
    assert len(result) == 2

    frags = [decode(d) for d in result]
    frags.sort(key=lambda f: f.index)
    assert [len(f.payload) for f in frags] == [1454, 60]
    assert [_outer_ip_len(d, h_ip=20) for d in result] == [1496, 106]
    assert frags[-1].rcs == crc32c(frame)
    assert _reassemble(result) == frame


# --- Fits: standard VXLAN path ----------------------------------------------


@pytest.mark.parametrize("h_ip,budget", [(20, 1464), (40, 1444)])
def test_exact_fit_returns_fits(h_ip, budget):
    frame = b"\x00" * budget
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=h_ip,
        max_frame_len=budget + 100,
        max_fragments=2,
    )
    assert result == Fits()


@pytest.mark.parametrize("h_ip,budget", [(20, 1464), (40, 1444)])
def test_one_byte_over_fragments(h_ip, budget):
    frame = b"\x00" * (budget + 1)
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=h_ip,
        max_frame_len=budget + 1,
        max_fragments=2,
    )
    assert isinstance(result, list)
    assert len(result) == 2
    assert _reassemble(result) == frame


# --- Tagged frames (1518 bytes) ---------------------------------------------


def test_tagged_1518_byte_frame_fragments_within_pmtu():
    frame = b"\x01" * 1518
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=1518,
        max_fragments=2,
    )
    assert isinstance(result, list)
    for datagram in result:
        assert _outer_ip_len(datagram, h_ip=20) <= 1500
    assert _reassemble(result) == frame


def test_tagged_1518_byte_frame_dropped_when_max_frame_len_too_small():
    frame = b"\x01" * 1518
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=1517,
        max_fragments=2,
    )
    assert isinstance(result, Drop)


# --- PMTU change between calls ----------------------------------------------


def test_pmtu_change_between_calls_changes_slicing():
    frame = b"\x02" * 1600
    result_1500 = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=1600,
        max_fragments=2,
    )
    result_9000 = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=9000,
        h_ip=20,
        max_frame_len=1600,
        max_fragments=2,
    )
    assert isinstance(result_1500, list)
    assert result_9000 == Fits()
    assert len(decode(result_1500[0]).payload) == slice_budget(1500, h_ip=20)


# --- PMTU parametrization: 1280 (IPv6 minimum), 1500, 9000 ------------------


@pytest.mark.parametrize("pmtu,h_ip", [(1280, 40), (1500, 20), (9000, 20)])
def test_every_emitted_ip_packet_fits_pmtu(pmtu, h_ip):
    budget = vxlan_frame_budget(pmtu, h_ip)
    frame = b"\x03" * (budget + 1)
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=pmtu,
        h_ip=h_ip,
        max_frame_len=budget + 1,
        max_fragments=2,
    )
    assert isinstance(result, list)
    for datagram in result:
        assert _outer_ip_len(datagram, h_ip=h_ip) <= pmtu
    assert _reassemble(result) == frame


def test_frame_larger_than_1514_at_9000_pmtu():
    frame = b"\x04" * 10000
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=9000,
        h_ip=20,
        max_frame_len=10000,
        max_fragments=2,
    )
    assert isinstance(result, list)
    assert len(result) == 2
    for datagram in result:
        assert _outer_ip_len(datagram, h_ip=20) <= 9000
    assert _reassemble(result) == frame


def test_min_pmtu_cannot_carry_max_frame_len_in_max_fragments_is_rejected():
    # At the IPv6 minimum PMTU of 1280, two fragments cannot carry a 9000-byte frame.
    result = fragment_frame(
        b"\x05" * 9000,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1280,
        h_ip=40,
        max_frame_len=9000,
        max_fragments=2,
    )
    assert isinstance(result, Drop)


# --- Rejection branches ------------------------------------------------------


def test_non_positive_slice_budget_is_dropped():
    result = fragment_frame(
        b"\x06" * 20,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=10,
        h_ip=0,
        max_frame_len=100,
        max_fragments=4,
    )
    assert isinstance(result, Drop)


def test_frame_length_over_max_is_dropped():
    budget = vxlan_frame_budget(1500, h_ip=20)
    frame = b"\x07" * (budget + 1)
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=budget,  # smaller than the actual frame length
        max_fragments=2,
    )
    assert isinstance(result, Drop)


def test_fragment_count_over_max_is_dropped():
    c = slice_budget(1500, h_ip=20)
    frame = b"\x08" * (c * 3)  # needs 3 fragments
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=len(frame),
        max_fragments=2,
    )
    assert isinstance(result, Drop)


# --- No default PMTU exists in the code -------------------------------------


def test_no_default_pmtu_on_fragment_frame():
    sig = inspect.signature(fragment_frame)
    assert sig.parameters["pmtu"].default is inspect.Parameter.empty


def test_no_default_pmtu_on_budget_helpers():
    assert inspect.signature(vxlan_frame_budget).parameters["pmtu"].default is inspect.Parameter.empty
    assert inspect.signature(slice_budget).parameters["pmtu"].default is inspect.Parameter.empty


def test_fragment_frame_requires_pmtu_keyword():
    with pytest.raises(TypeError):
        fragment_frame(
            b"\x00" * 2000,
            vni=VNI,
            frame_id=FRAME_ID,
            h_ip=20,
            max_frame_len=2000,
            max_fragments=2,
        )


# --- Header fields and ascending index order --------------------------------


def test_fragments_carry_vni_frame_id_and_ascending_index():
    c = slice_budget(1500, h_ip=20)
    frame = b"\x09" * (c + 11)  # one byte over the VXLAN budget (budget == c + 10)
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=len(frame),
        max_fragments=2,
    )
    assert isinstance(result, list)
    frags = [decode(d) for d in result]
    assert [f.index for f in frags] == list(range(len(frags)))
    for f in frags:
        assert f.vni == VNI
        assert f.frame_id == FRAME_ID
        assert f.count == len(frags)


def test_nonfinal_slices_are_all_c_bytes():
    c = slice_budget(1500, h_ip=20)
    frame = b"\x0a" * (c * 2 - 1)  # two fragments: full C, then C - 1
    result = fragment_frame(
        frame,
        vni=VNI,
        frame_id=FRAME_ID,
        pmtu=1500,
        h_ip=20,
        max_frame_len=len(frame),
        max_fragments=2,
    )
    frags = sorted((decode(d) for d in result), key=lambda f: f.index)
    assert len(frags[0].payload) == c
    assert len(frags[1].payload) == c - 1
    assert len(frags[1].payload) + RCS_LEN + HEADER_LEN == len(result[1])
