"""CRC-32C (Castagnoli) per DESIGN.md "Prototype wire format".

Reflected input/output, initial register and final XOR both 0xffffffff.
Check value for ASCII b"123456789" is 0xe3069283.
"""

_POLY = 0x82F63B78  # reversed Castagnoli polynomial 0x1EDC6F41


def _build_table() -> tuple[int, ...]:
    table = []
    for byte in range(256):
        crc = byte
        for _ in range(8):
            crc = (crc >> 1) ^ _POLY if crc & 1 else crc >> 1
        table.append(crc)
    return tuple(table)


_TABLE = _build_table()


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for byte in data:
        crc = _TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF
