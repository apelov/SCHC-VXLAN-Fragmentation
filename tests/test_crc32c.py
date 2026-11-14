from schcvx.crc32c import crc32c


def test_check_value():
    assert crc32c(b"123456789") == 0xE3069283


def test_empty_input():
    assert crc32c(b"") == 0x00000000


def test_deterministic_and_input_sensitive():
    assert crc32c(b"hello") == crc32c(b"hello")
    assert crc32c(b"hello") != crc32c(b"hellp")
