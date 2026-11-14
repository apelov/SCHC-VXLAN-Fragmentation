"""Reference model for draft-vladyka-schc-vxlan-fragmentation."""

from .crc32c import crc32c
from .wire import (
    HEADER_LEN,
    RCS_LEN,
    RULE_ID_0,
    UDP_LEN,
    VXLAN_HDR_LEN,
    Fragment,
    MalformedFragment,
    decode,
    encode,
)

__all__ = [
    "HEADER_LEN",
    "RCS_LEN",
    "RULE_ID_0",
    "UDP_LEN",
    "VXLAN_HDR_LEN",
    "Fragment",
    "MalformedFragment",
    "crc32c",
    "decode",
    "encode",
]
