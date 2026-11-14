import pytest

from schcvx.wire import (
    HEADER_LEN,
    RCS_LEN,
    Fragment,
    MalformedFragment,
    decode,
    encode,
)


def _nonfinal(**overrides):
    fields = {
        "rule_id": 0,
        "vni": 0x010203,
        "frame_id": 0x1122334455667788,
        "index": 0,
        "count": 2,
        "payload": b"hello",
        "rcs": None,
    }
    fields.update(overrides)
    return Fragment(**fields)


def _final(**overrides):
    fields = {
        "rule_id": 0,
        "vni": 0x010203,
        "frame_id": 0x1122334455667788,
        "index": 1,
        "count": 2,
        "payload": b"world",
        "rcs": 0xDEADBEEF,
    }
    fields.update(overrides)
    return Fragment(**fields)


def test_roundtrip_nonfinal():
    frag = _nonfinal()
    assert decode(encode(frag)) == frag


def test_roundtrip_final():
    frag = _final()
    assert decode(encode(frag)) == frag


def test_byte_offsets():
    frag = _nonfinal(
        rule_id=0x00,
        vni=0xABCDEF,
        frame_id=0x0102030405060708,
        index=0,
        count=3,
        payload=b"X",
    )
    wire = encode(frag)
    assert wire[0] == 0x00
    assert wire[1:4] == bytes.fromhex("abcdef")
    assert wire[4:12] == bytes.fromhex("0102030405060708")
    assert wire[12] == 0
    assert wire[13] == 3
    assert wire[14:] == b"X"


def test_nonfinal_has_no_trailer():
    frag = _nonfinal(payload=b"abc")
    wire = encode(frag)
    assert len(wire) == HEADER_LEN + 3


def test_final_rcs_appended_big_endian_at_end():
    frag = _final(payload=b"abc", rcs=0x01020304)
    wire = encode(frag)
    assert len(wire) == HEADER_LEN + 3 + RCS_LEN
    assert wire[HEADER_LEN : HEADER_LEN + 3] == b"abc"
    assert wire[-RCS_LEN:] == bytes.fromhex("01020304")


def test_encode_requires_rcs_on_final():
    frag = _final(rcs=None)
    with pytest.raises(ValueError):
        encode(frag)


def test_encode_rejects_rcs_on_nonfinal():
    frag = _nonfinal(rcs=0x01020304)
    with pytest.raises(ValueError):
        encode(frag)


def test_encode_rejects_empty_payload():
    frag = _nonfinal(payload=b"")
    with pytest.raises(ValueError):
        encode(frag)


def test_encode_rejects_index_geq_count():
    frag = _nonfinal(index=2, count=2)
    with pytest.raises(ValueError):
        encode(frag)


def test_encode_rejects_count_below_two():
    frag = _nonfinal(index=0, count=1, rcs=0x01020304)
    with pytest.raises(ValueError):
        encode(frag)


@pytest.mark.parametrize(
    "field,value",
    [
        ("rule_id", -1),
        ("rule_id", 256),
        ("vni", -1),
        ("vni", 1 << 24),
        ("frame_id", -1),
        ("frame_id", 1 << 64),
        ("index", -1),
        ("index", 256),
        ("count", 256),
    ],
)
def test_encode_rejects_out_of_range_fields(field, value):
    frag = _nonfinal(**{field: value})
    with pytest.raises(ValueError):
        encode(frag)


def test_decode_short_header():
    with pytest.raises(MalformedFragment) as exc:
        decode(b"\x00" * (HEADER_LEN - 1))
    assert exc.value.reason == "short header"


def test_decode_count_less_than_two():
    wire = bytearray(encode(_nonfinal(count=2)))
    wire[13] = 1
    with pytest.raises(MalformedFragment) as exc:
        decode(bytes(wire))
    assert exc.value.reason == "count < 2"


def test_decode_index_geq_count():
    wire = bytearray(encode(_nonfinal(count=3, index=0)))
    wire[12] = 3
    with pytest.raises(MalformedFragment) as exc:
        decode(bytes(wire))
    assert exc.value.reason == "index >= count"


def test_decode_empty_slice_nonfinal():
    header = bytes(HEADER_LEN)
    wire = bytearray(header)
    wire[13] = 2  # count = 2, index = 0 -> nonfinal, no payload bytes follow
    with pytest.raises(MalformedFragment) as exc:
        decode(bytes(wire))
    assert exc.value.reason == "empty slice"


def test_decode_final_too_short_for_rcs():
    header = bytearray(HEADER_LEN)
    header[12] = 1  # index
    header[13] = 2  # count -> final fragment
    wire = bytes(header) + b"ab"  # only 2 bytes, need RCS_LEN + 1 = 5
    with pytest.raises(MalformedFragment) as exc:
        decode(wire)
    assert exc.value.reason == "final fragment too short to hold slice plus RCS"


def test_decode_final_exact_minimum_slice_plus_rcs():
    header = bytearray(HEADER_LEN)
    header[12] = 1
    header[13] = 2
    wire = bytes(header) + b"a" + (0x01020304).to_bytes(RCS_LEN, "big")
    frag = decode(wire)
    assert frag.payload == b"a"
    assert frag.rcs == 0x01020304
