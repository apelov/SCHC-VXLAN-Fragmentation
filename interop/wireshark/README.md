# schcvx: Wireshark dissector for SCHC VXLAN fragmentation

`schcvx.lua` dissects the UDP payload of the SCHC VXLAN binding in
`draft/draft-vladyka-schc-vxlan-fragmentation-00.md`. It is written from the draft
only and is independent of the C and Rust implementations. It targets Lua 5.4 as shipped with
Wireshark 4.x (tested with 4.6.9).

## What it does

- Registers on UDP port 60789 (changeable in the preferences).
- Classifies a datagram by its first byte: equal to the configured NoCompress C/D RuleID means an
  unfragmented SCHC Packet; anything else is parsed as a fragment.
- Unfragmented: shows the C/D RuleID and hands the rest (VXLAN header and Ethernet frame) to
  Wireshark's `vxlan` dissector, so the VNI and the inner frame are decoded. A packet shorter than
  23 bytes is flagged.
- Fragment: decodes RuleID, DTag, FCN (first flag, countdown, K for first fragments), the RCS of an
  All-1, and the tile. Sender-Abort (header-only All-1) is recognised.
- Reassembly (on by default): fragments are collected per (source address, destination address,
  RuleID, DTag) on the first pass. Order does not matter, so an All-1 that arrives before the first
  fragment works. When the first fragment, all countdown positions and the All-1 are present, the
  tiles are joined in sender order (first, countdown n-1 down to 0, All-1) and the CRC-32C is checked
  against the RCS. The completing frame gets a "Reassembled SCHC Packet" data source, the RCS status,
  and the VXLAN and inner dissection. Earlier fragments show "Reassembled in frame N".
- The Info column looks like `SCHC frag DTag=0x00000001 FCN=1000 (first, K=2)` or
  `SCHC unfragmented VNI=100`, followed by `| <inner protocol info>` where an inner frame is decoded.

## Installation

Try it without installing:

```
tshark -X lua_script:interop/wireshark/schcvx.lua -r capture.pcap -Y schcvx
wireshark -X lua_script:interop/wireshark/schcvx.lua capture.pcap
```

Permanent install: copy `schcvx.lua` into the personal Lua plugins folder and restart Wireshark.

- macOS and Linux: `~/.local/lib/wireshark/plugins/` (older versions: `~/.config/wireshark/plugins/`)
- Windows: `%APPDATA%\Wireshark\plugins\`

`Help > About Wireshark > Folders` shows the exact path. Lua must be enabled (it is by default).

## Preferences (Edit > Preferences > Protocols > SCHCVX, or `-o schcvx.<name>:<value>`)

| Preference | Default | Meaning |
| --- | --- | --- |
| `schcvx.port` | 60789 | UDP port of the binding |
| `schcvx.variant` | Compact | Compact (RuleID 4, DTag 32, FCN 4 bits) or Extended (RuleID 8, DTag 60, FCN 4 bits); `-o schcvx.variant:2` selects Extended |
| `schcvx.cd_rule` | 0 | 8-bit NoCompress C/D RuleID |
| `schcvx.frag_rule` | 1 | Configured fragmentation RuleID; others are flagged and not reassembled. 65535 accepts any |
| `schcvx.reassemble` | on | Reassemble and verify the RCS |

The variant is a property of the association and cannot be detected from the wire; set it to match
the traffic you look at. A capture that mixes both variants needs two passes with different settings.

## Field names

| Filter | Meaning |
| --- | --- |
| `schcvx.kind` | `unfragmented`, `regular`, `all-1`, `sender-abort` |
| `schcvx.rule_id` | Fragmentation RuleID |
| `schcvx.dtag` | DTag (64-bit) |
| `schcvx.fcn` | FCN value (0-15); `schcvx.fcn.bits` the 4 bits as text |
| `schcvx.fcn.first` | First-fragment flag |
| `schcvx.fcn.countdown` | Remaining regular fragments after this one (not for All-1) |
| `schcvx.k` | Number of fragments, on first fragments (countdown + 2) |
| `schcvx.rcs` | RCS from the All-1 or the reassembled packet |
| `schcvx.rcs.status` | `good` or `bad` (set on the completing frame) |
| `schcvx.rcs.calculated` | CRC-32C computed over the reassembled packet |
| `schcvx.cd_rule_id` | C/D RuleID |
| `schcvx.vni` | VNI as read from a first fragment payload |
| `schcvx.payload_len` | Fragment payload length (or SCHC Packet length for unfragmented) |
| `schcvx.payload` | Fragment payload bytes |
| `schcvx.reassembled_in` | Frame number of the completing fragment |
| `schcvx.reassembled_len` | Reassembled SCHC Packet length |

Example: `schcvx.rcs.status == "good"` lists completed packets, `schcvx.rcs.status == "bad"` the
failures. The `vxlan.vni` and `eth` fields from the built-in dissectors work on both unfragmented
and reassembled packets.

## Expert information

Invalid FCN (6 or 7); All-1 of header length plus 1 to 4 bytes; Sender-Abort (warning); regular
fragment with an empty tile or a datagram shorter than the header; first fragment without the C/D
RuleID or with fewer than 9 payload bytes; bad RCS; unfragmented packet under 23 bytes; fragmentation
RuleID different from the configured one; conflicting duplicates (identical duplicates are ignored)
and positions outside the expected set; fragments that never complete (note).

## Testing notes

- `interop/results/2026-11-15-c-to-rust-step1.pcap`: unfragmented ARP and ICMP decode with VNI 100;
  the two-fragment packets reassemble to 1244 and 1514 byte Ethernet frames with a good RCS (first
  fragmented packet: DTag 0, RCS 0xffaea246).
- Vectors from `draft/revised/wire-vectors.json`: a throwaway standard-library Python script (not
  committed) wrote a classic pcap with Ethernet, IPv4 and UDP headers around each `udp_payload_hex`,
  for both variants (keys `32` and `60`), once with the All-1 first and once in order. Both variants
  reassemble with RCS 0x94dc7a0e, good. A second script with malformed datagrams (short unfragmented
  packet, FCN 0110, Sender-Abort, short All-1, bad first fragments, flipped RCS bit) produced the
  expected expert messages.

## Limitations

- The wire variant is a preference, not detected. The configured values stand in for the
  association; the source UDP port is ignored for reassembly, as in the draft, but the key does use
  IP source and destination addresses.
- The dissector shows what is on the wire; it does not emulate receiver timers, retirement, DTag
  quarantine, or the ordered receiver that drops reordered fragments. It always behaves like the
  tolerant receiver when reassembling. Capture time and deadlines are not evaluated.
- A DTag reused for a new packet while an earlier one is incomplete merges into the same entry; a
  conflicting duplicate is reported but the first fragment seen wins.
- UDP and IP checksums are left to Wireshark's own dissectors. The VNI is not authorised against
  anything.
- Only 8-bit C/D RuleIDs and the two draft variants are supported (no other DTag widths).
- The Source and Destination columns of reassembled or unfragmented packets show the inner frame
  addresses because the built-in vxlan and Ethernet dissectors overwrite them.
- A fragment must be one per frame; several SCHC datagrams in one capture frame are not handled.
