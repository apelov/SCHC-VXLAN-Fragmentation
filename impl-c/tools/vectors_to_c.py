"""Render shared JSON fixtures as C data, without protocol encoding."""
import argparse
import json
from pathlib import Path


def render(data):
    lines = ['/* Generated; do not edit. */', '#ifndef SCHCVX_TEST_VECTORS_H',
             '#define SCHCVX_TEST_VECTORS_H', '#include <stddef.h>', '#include <stdint.h>',
             'struct vector_datagram { const uint8_t *data; size_t len, tile_len, ip_len; };',
             'struct wire_vector { unsigned bits; size_t packet_len; uint32_t rcs;',
             '    const struct vector_datagram *datagrams; size_t count; };']

    def array(name, encoded):
        raw = bytes.fromhex(encoded)
        if not raw:
            raise ValueError('empty datagram')
        lines.append(f'static const uint8_t {name}[] = {{')
        for offset in range(0, len(raw), 12):
            lines.append('    ' + ', '.join(f'0x{b:02x}' for b in raw[offset:offset+12]) + ',')
        lines.append('};')

    array('unfragmented_vector', data['unfragmented']['udp_payload_hex'])
    entries = []
    for bits in (32, 60):
        v = data[str(bits)]
        payloads = v['udp_payload_hex']
        if len(payloads) != len(v['tile_lengths']) or len(payloads) != len(v['ip_lengths']):
            raise ValueError('inconsistent metadata lengths')
        for i, encoded in enumerate(payloads):
            array(f'vector_{bits}_{i}', encoded)
        lines.append(f'static const struct vector_datagram datagrams_{bits}[] = {{')
        for i, (tile, ip) in enumerate(zip(v['tile_lengths'], v['ip_lengths'])):
            lines.append(f'    {{vector_{bits}_{i}, sizeof(vector_{bits}_{i}), {int(tile)}, {int(ip)}}},')
        lines.append('};')
        entries.append(f"    {{{bits}, {int(v['packet_bytes'])}, UINT32_C(0x{int(v['rcs'], 16):08x}), datagrams_{bits}, {len(payloads)}}},")
    lines.extend(['static const struct wire_vector wire_vectors[] = {', *entries, '};', '#endif', ''])
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(render(json.loads(args.input.read_text(encoding='utf-8'))), encoding='utf-8')


if __name__ == '__main__':
    main()
