# Fragment Format

This section defines the UDP payload of each fragment. The wire values are provisional prototype choices. The format has no wire compatibility with {{RFC8724}}.

All multibyte integers use network byte order. The UDP destination port, configured locally, selects this format. The RuleID comes first, as in other SCHC messages, so that the format can later share a SCHC UDP port with other SCHC uses and be selected by RuleID.

## Header

Every fragment starts with a 14-byte header, followed by a nonempty slice of the original frame. The final fragment ends with a 4-byte RCS.

~~~~
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    RuleID     |                      VNI                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                           Frame ID                            +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Index     |     Count     |   Slice (variable) ...
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
|                                                               |
~                              ...                              ~
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          RCS (final fragment only, 4 bytes)                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
~~~~
{: #fig-format title="Fragment format"}

| Byte offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | RuleID, configured per peer association; value 0 in the prototype |
| 1 | 3 | VNI, with the same meaning as in the associated VXLAN deployment |
| 4 | 8 | Frame ID, unique across active and recently retired frames within the configured peer association and VNI |
| 12 | 1 | Fragment index, zero-based |
| 13 | 1 | Fragment count, 2 through the configured maximum |
| 14 | variable | Nonempty slice of the original frame |
| End of final fragment | 4 | RCS, present only if index equals count minus one |
{: #tab-fields title="Fragment fields"}

There is no original-length field. The receiver relies on the fragment count, the equal-length nonfinal slices, the configured maximum frame length and the RCS.

The RuleID is one byte, so that the header stays byte-aligned; a zero-bit RuleID is not used. Section 6 of {{RFC8724}} leaves the RuleID size to the Profile. The RuleID is configured per peer association and is 0 in the prototype.

## Reassembly Check Sequence

The final fragment, and only the final fragment, carries a 4-byte RCS after its slice. It is the CRC-32C of the original frame bytes in wire order, serialized big-endian. The RCS guards against splicing slices of different frames, for example after a restart. It covers the frame bytes only and checks reassembly integrity; it does not protect the header.

CRC-32C is defined as follows:

* Polynomial: the Castagnoli polynomial, 0x1edc6f41 (0x82f63b78 in reflected form).
* Input and output are reflected.
* The initial register value is 0xffffffff.
* The final XOR value is 0xffffffff.
* The check value for the ASCII string `123456789` is 0xe3069283.
* The result is serialized as four bytes, most significant byte first.

## UDP Checksum

A sender MUST generate the UDP checksum on every extension datagram, over IPv4 as well as IPv6. A receiver MUST drop an extension datagram whose UDP checksum is zero or incorrect. This differs from Section 5 of {{RFC7348}}, which recommends a zero UDP checksum for VXLAN. Here the UDP checksum is the only protection of the fragment header.

## UDP Port Selection

The UDP destination port is selected by configuration and identifies this format, distinct from the port used for ordinary VXLAN. Both peers are configured with the same port.

Open Issue: header versioning and UDP demultiplexing are not settled. The header has no version field. The port is locally configured in this version. The SCHC UDP port of {{I-D.ietf-schc-protocol-numbers}} is the candidate once assigned; that draft has no assigned value yet and does not define the UDP payload layout, so sharing the port is open, as is the allocation of RuleID values on a shared port. Any eventual IANA request is also open.

## Provisional Status

The field sizes, offsets and the RuleID value in this section are provisional prototype choices and may change before any interoperable specification. The reference model in `src/schcvx/wire.py` implements exactly this layout.
