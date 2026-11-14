"""Wire codec for the prototype fragment header, per DESIGN.md "Prototype wire format".

Header layout (network byte order, 14 bytes):

| Byte offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | RuleID |
| 1 | 3 | VNI |
| 4 | 8 | Frame ID |
| 12 | 1 | Fragment index (zero-based) |
| 13 | 1 | Fragment count |
| 14 | variable | Nonempty slice of the original frame |
| end of final fragment | 4 | RCS, present only if index == count - 1 |
"""

from dataclasses import dataclass

HEADER_LEN = 14
RCS_LEN = 4
UDP_LEN = 8
VXLAN_HDR_LEN = 8
RULE_ID_0 = 0

_RULE_ID_MAX = 0xFF
_VNI_MAX = 0xFFFFFF
_FRAME_ID_MAX = 0xFFFFFFFFFFFFFFFF
_INDEX_MAX = 0xFF
_COUNT_MAX = 0xFF


class MalformedFragment(Exception):
    """Raised by decode() when a datagram does not hold a valid fragment."""

    def __init__(self, reason: str):
        super().__init__(reason)
        self.reason = reason


@dataclass(frozen=True)
class Fragment:
    rule_id: int
    vni: int
    frame_id: int
    index: int
    count: int
    payload: bytes
    rcs: int | None


def encode(fragment: Fragment) -> bytes:
    if not 0 <= fragment.rule_id <= _RULE_ID_MAX:
        raise ValueError(f"rule_id out of range: {fragment.rule_id}")
    if not 0 <= fragment.vni <= _VNI_MAX:
        raise ValueError(f"vni out of range: {fragment.vni}")
    if not 0 <= fragment.frame_id <= _FRAME_ID_MAX:
        raise ValueError(f"frame_id out of range: {fragment.frame_id}")
    if not 0 <= fragment.index <= _INDEX_MAX:
        raise ValueError(f"index out of range: {fragment.index}")
    if not 2 <= fragment.count <= _COUNT_MAX:
        raise ValueError(f"count out of range: {fragment.count}")
    if fragment.index >= fragment.count:
        raise ValueError(f"index {fragment.index} >= count {fragment.count}")
    if not fragment.payload:
        raise ValueError("payload must be nonempty")

    is_final = fragment.index == fragment.count - 1
    if is_final and fragment.rcs is None:
        raise ValueError("final fragment requires rcs")
    if not is_final and fragment.rcs is not None:
        raise ValueError("non-final fragment must not set rcs")

    header = bytearray(HEADER_LEN)
    header[0] = fragment.rule_id
    header[1:4] = fragment.vni.to_bytes(3, "big")
    header[4:12] = fragment.frame_id.to_bytes(8, "big")
    header[12] = fragment.index
    header[13] = fragment.count

    out = bytes(header) + fragment.payload
    if is_final:
        out += fragment.rcs.to_bytes(RCS_LEN, "big")
    return out


def decode(datagram: bytes) -> Fragment:
    if len(datagram) < HEADER_LEN:
        raise MalformedFragment("short header")

    rule_id = datagram[0]
    vni = int.from_bytes(datagram[1:4], "big")
    frame_id = int.from_bytes(datagram[4:12], "big")
    index = datagram[12]
    count = datagram[13]

    if count < 2:
        raise MalformedFragment("count < 2")
    if index >= count:
        raise MalformedFragment("index >= count")

    remaining = len(datagram) - HEADER_LEN
    is_final = index == count - 1

    if is_final:
        if remaining < RCS_LEN + 1:
            raise MalformedFragment("final fragment too short to hold slice plus RCS")
        payload = datagram[HEADER_LEN:-RCS_LEN]
        rcs = int.from_bytes(datagram[-RCS_LEN:], "big")
    else:
        if remaining == 0:
            raise MalformedFragment("empty slice")
        payload = datagram[HEADER_LEN:]
        rcs = None

    return Fragment(
        rule_id=rule_id,
        vni=vni,
        frame_id=frame_id,
        index=index,
        count=count,
        payload=bytes(payload),
        rcs=rcs,
    )
