"""Reference sender: budget and slicing, per DESIGN.md "MTU and sender algorithm".

No default PMTU exists here: callers must supply `pmtu` for every call, since
DESIGN.md states PMTU is a per-peer, per-direction sender configuration value
with no default in the document.
"""

import math
from dataclasses import dataclass

from .crc32c import crc32c
from .wire import HEADER_LEN, RCS_LEN, RULE_ID_0, UDP_LEN, VXLAN_HDR_LEN, Fragment, encode


@dataclass(frozen=True)
class Fits:
    """The frame fits standard VXLAN; the caller should emit it unfragmented."""


@dataclass(frozen=True)
class Drop:
    """The frame cannot be sent, fragmented or not, under the given configuration."""

    reason: str


def vxlan_frame_budget(pmtu: int, h_ip: int) -> int:
    """Largest frame carried by standard VXLAN: PMTU - H_IP - UDP - VXLAN header."""
    return pmtu - h_ip - UDP_LEN - VXLAN_HDR_LEN


def slice_budget(pmtu: int, h_ip: int) -> int:
    """C: the per-fragment slice budget, same for every fragment of a frame."""
    return pmtu - h_ip - UDP_LEN - HEADER_LEN - RCS_LEN


def fragment_frame(
    frame: bytes,
    *,
    vni: int,
    frame_id: int,
    pmtu: int,
    h_ip: int,
    max_frame_len: int,
    max_fragments: int,
) -> list[bytes] | Fits | Drop:
    frame_len = len(frame)

    if frame_len <= vxlan_frame_budget(pmtu, h_ip):
        return Fits()

    c = slice_budget(pmtu, h_ip)
    if c <= 0:
        return Drop("slice budget is not positive")

    if frame_len < 1 or frame_len > max_frame_len:
        return Drop("frame length outside configured range")

    count = math.ceil(frame_len / c)
    if count < 2 or count > max_fragments:
        return Drop("fragment count outside configured range")

    rcs = crc32c(frame)
    datagrams = []
    for index in range(count):
        is_final = index == count - 1
        start = index * c
        end = frame_len if is_final else start + c
        fragment = Fragment(
            rule_id=RULE_ID_0,
            vni=vni,
            frame_id=frame_id,
            index=index,
            count=count,
            payload=frame[start:end],
            rcs=rcs if is_final else None,
        )
        datagrams.append(encode(fragment))
    return datagrams
