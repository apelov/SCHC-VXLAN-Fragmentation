"""Executable wire-vector and bounded reassembly checks; not a production receiver."""
import itertools
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src'))
from schcvx.crc32c import crc32c


def encode(rule, tag, fcn, payload, bits=32, rcs=None):
    rb = 4 if bits == 32 else 8
    if not (0 <= rule < 2**rb and 0 <= tag < 2**bits and 0 <= fcn < 16):
        raise ValueError('field overflow')
    header = ((rule << (bits+4)) | (tag << 4) | fcn).to_bytes((rb+bits+4)//8, 'big')
    return header + (rcs.to_bytes(4, 'big') if rcs is not None else b'') + payload


def decode(data, bits=32):
    h = 5 if bits == 32 else 9
    if len(data) < h:
        raise ValueError('short header')
    word = int.from_bytes(data[:h], 'big')
    rule, tag, fcn = word >> (bits+4), (word >> 4) & (2**bits-1), word & 15
    if fcn == 15 and len(data) == h:
        return rule, tag, 'abort', b'', None
    if fcn in (6, 7) or len(data) < h + (5 if fcn == 15 else 1):
        raise ValueError('malformed')
    payload = data[h+4:] if fcn == 15 else data[h:]
    if 8 <= fcn <= 14 and (len(payload) < 9 or payload[0] != 0):
        raise ValueError('first fragment envelope')
    return rule, tag, fcn, payload, int.from_bytes(data[h:h+4], 'big') if fcn == 15 else None


def reassemble(messages, bits=32):
    slots = {}
    identity = None
    for message in messages:
        rule, tag, fcn, payload, rcs = decode(message, bits)
        if fcn == 'abort':
            raise ValueError('abort')
        if identity is not None and identity != (rule, tag):
            raise ValueError('mixed identity')
        identity = rule, tag
        key = 'final' if fcn == 15 else fcn & 7
        value = fcn, payload, rcs
        if key in slots and slots[key] != value:
            raise ValueError('conflicting duplicate')
        slots[key] = value
    first = [v for v in slots.values() if 8 <= v[0] <= 14]
    if len(first) != 1:
        return None
    n = first[0][0] & 7
    expected = set(range(n+1)) | {'final'}
    if set(slots) - expected:
        raise ValueError('unexpected position')
    if set(slots) != expected:
        return None
    tiles = [slots[i][1] for i in range(n, -1, -1)]
    final = slots['final']
    if len({len(t) for t in tiles}) != 1 or len(final[1]) > len(tiles[0]):
        raise ValueError('tile lengths')
    packet = b''.join(tiles) + final[1]
    if crc32c(packet) != final[2]:
        raise ValueError('RCS')
    return packet


def rejects(fn):
    try:
        fn()
    except ValueError:
        return
    raise AssertionError('expected rejection')


def main():
    assert crc32c(b'123456789') == 0xe3069283
    # Independent fixed bit-pattern anchors for both layouts.
    assert encode(1, 1, 8, b'').hex() == '1000000018'
    assert encode(1, 1, 8, b'', 60).hex() == '010000000000000018'
    for bits in (32, 60):
        for k in range(2, 9):
            packet = b'\0' + bytes(range(1, k*9))
            fcns = [8+k-2] + list(range(k-3, -1, -1)) + [15]
            messages = [encode(1, 1, f, packet[i*9:(i+1)*9], bits,
                               crc32c(packet) if f == 15 else None)
                        for i, f in enumerate(fcns)]
            for order in itertools.permutations(messages):
                assert reassemble(order, bits) == packet
            for i in range(k):
                assert reassemble(messages[:i]+messages[i+1:], bits) is None
            assert reassemble(messages+[messages[0]], bits) == packet
            bad = messages[-1][: -1] + bytes([messages[-1][-1] ^ 1])
            rejects(lambda: reassemble(messages+[bad], bits))
            rejects(lambda: reassemble(messages[:-1]+[bad], bits))
        h = 5 if bits == 32 else 9
        for length in range(h):
            rejects(lambda: decode(b'\0'*length, bits))
        for extra in range(1, 5):
            rejects(lambda: decode(encode(1, 1, 15, b'\0'*extra, bits), bits))
        for f in (6, 7):
            rejects(lambda: decode(encode(1, 1, f, b'x', bits), bits))
    frame = bytes(i % 256 for i in range(1514))
    packet = b'\0' + bytes.fromhex('0800000000303900') + frame
    vectors = {}
    for bits in (32, 60):
        h = 5 if bits == 32 else 9
        c = 1500-20-8-h-4
        parts = [packet[:c], packet[c:]]
        messages = [encode(1, 1, 8, parts[0], bits),
                    encode(1, 1, 15, parts[1], bits, crc32c(packet))]
        assert reassemble(messages, bits) == packet
        vectors[str(bits)] = {'packet_bytes': len(packet), 'rcs': f'{crc32c(packet):08x}',
                             'tile_lengths': list(map(len, parts)),
                             'ip_lengths': [28+len(m) for m in messages],
                             'udp_payload_hex': [m.hex() for m in messages]}
    assert vectors['32']['ip_lengths'] == [1496, 97]
    assert vectors['60']['ip_lengths'] == [1496, 105]
    for p,h,c,k in [(1500,20,1463,7),(1500,40,1443,7),(1280,40,1223,8)]:
        assert p-h-8-9 == c and (9023+c-1)//c == k
    target = Path(__file__).with_name('wire-vectors.json')
    rendered = json.dumps(vectors, indent=2)+'\n'
    if '--write' in sys.argv:
        target.write_text(rendered)
    else:
        assert target.read_text() == rendered, 'stale vectors'
    print('PASS: both variants, all K=2..8 permutations, loss, duplicates, corruption, malformed input and MTU vectors')


if __name__ == '__main__':
    main()
