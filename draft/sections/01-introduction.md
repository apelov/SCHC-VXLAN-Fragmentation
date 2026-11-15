# Introduction

VXLAN {{RFC7348}} carries Ethernet frames in UDP datagrams. When the encapsulated frame exceeds the underlay IP path MTU (PMTU), standard VXLAN cannot carry it, and VTEPs MUST NOT fragment VXLAN packets (Section 4.3 of {{RFC7348}}). This document specifies an extension that fragments such a frame into several UDP datagrams between a pair of VTEPs, reassembles it at the receiver, and delivers it only after successful reassembly. General guidance on UDP usage and on MTU issues with tunnels is in {{RFC8085}} and {{RFC4459}}.

This version is a framing proposal, presented to seek working group feedback and adoption. Its wire format is provisional and several parameters are left as open issues.

The requirements are:

* Preserve the full input Ethernet frame as presented to the tunnel, including any tags carried by the tunnel, and excluding the Ethernet FCS.
* Keep standard VXLAN for frames that fit.
* Deliver a fragmented frame only after successful reassembly, with bounded waiting time and bounded receiver state.
* Tolerate reordering of fragments.
* Use no acknowledgments and no retransmissions in the data path.

The following are non-goals:

* Acknowledgment, retransmission or any other loss recovery. A frame with a missing fragment is discarded at its deadline.
* Inter-frame ordering. Frames are delivered as they complete, and the design gives no ordering guarantee between frames on the ordinary path and frames on the fragmented path.
* Header compression, which is outside the scope of this document.

Line rate is a performance target that depends on the traffic mix, the link rate and the packet expansion. This document makes no throughput claim; any such figure is pending measurement.

The first version targets statically provisioned unicast VTEP pairs in a managed fabric. The wire values in this document are provisional prototype choices.

# Terminology

{::boilerplate bcp14-tagged}

The following terms are used in this document.

VTEP:
: VXLAN Tunnel End Point, as in {{RFC7348}}.

PMTU:
: The underlay IP path MTU, excluding outer Ethernet overhead. It is a sender-side configuration value per peer and direction.

Slice:
: A nonempty contiguous part of the original Ethernet frame carried in one fragment.

Frame ID:
: An 8-byte identifier, unique across active and recently retired frames within the configured peer association and VNI.

RCS:
: Reassembly Check Sequence, a CRC-32C over the original frame bytes, carried at the end of the final fragment.

Δ:
: The discard deadline. A reassembly entry expires at the first arrival time plus Δ.

L:
: A configured upper bound on the arrival spread of all copies of fragments of one frame, including duplicates. Retirement records are kept for L after retirement.

Peer association:
: The configured relationship between a local tunnel endpoint and a remote VTEP, with its RuleID, PMTU, port, and capabilities.

# Placement and Interoperability

Transmit placement is after bridge forwarding and before tunnel encapsulation. Receive placement is after tunnel classification and before bridge ingress. Both paths feed the same VNI-to-bridge mapping. A VTEP learns source MAC addresses only from complete frames.

A sender behaves as follows:

* If the frame fits, it emits RFC 7348 VXLAN.
* If the frame exceeds the VXLAN payload budget and both peers are configured for the extension, it encapsulates each slice in a separate UDP datagram sent to a distinct, configured destination port.
* If the extension is unavailable or the configured limits cannot carry the frame, it drops the oversized frame and increments a local counter (drop-and-count).

Scope is unicast between VTEPs. BUM traffic is in scope only through ingress replication to individually provisioned unicast peers. Each copy uses that peer's capabilities and PMTU.

The extension carries the semantics of the Ethernet overlay VNI but uses its own UDP payload format. It does not fragment an already encapsulated VXLAN packet at IP. The configured UDP destination port selects this format. The sender uses the same source UDP port and outer five-tuple for every fragment of a frame, so that ordinary flow-hash affinity applies, and derives the source port from the inner frame as it does for ordinary VXLAN (Section 5 of {{RFC7348}}). The receiver identifies a configured peer independently of the source UDP port. The protocol tolerates reordering even when path affinity fails.

NAT is outside the target deployment. Where one is present: a rewritten source port does not matter; a rewritten outer source address must be the address configured for the peer association; and because the two directions do not form one symmetric flow, the destination port needs a static mapping.

The wire format and all values in this document are provisional prototype choices and are subject to change. The UDP port is locally configured in this version.

Open Issue: UDP demultiplexing, header versioning, any IANA request, and the need for peer capability provisioning before oversized frames are sent are not settled in this version.

# Relationship to SCHC

Section 8.4.1 of {{RFC8724}} defines the No-ACK fragmentation mode. It assumes in-order delivery of fragments and concludes reassembly on All-1. The indexed receiver in this document can accept the final fragment first and wait for missing indices, so No-ACK does not fit.

A Profile can set the FCN size above one bit and define FCN values other than All-1 (Section 8.4.1 of {{RFC8724}}), so a No-ACK Profile with several fragments is possible. It still lacks what an out-of-order receiver needs. The All-1 value identifies the end, but nothing in the fragments states how many fragments the frame has. A receiver that holds the All-1 fragment and several others cannot tell whether it holds all of them, unless the Profile fixes the fragment count; here the count varies with the frame length. Carrying the count in every fragment, with an ascending index, lets any fragment, including the final one, open an entry with a bitmap of the right size.

This document defines a new SCHC fragmentation mode, named Out-of-Order No-ACK, with a UDP/Ethernet binding. It is a new mode defined by its differences from No-ACK, not an extension of No-ACK. Open Issue: how the mode integrates with the SCHC framework is pending working-group agreement.

The mode differs from No-ACK in three ways:

* **Index and count instead of FCN.** In No-ACK, the FCN All-1 value only signals the last tile (Section 8.2.4 of {{RFC8724}}), the recommended FCN size is one bit, and the meaning of other values is left to the Profile (Section 8.4.1 of {{RFC8724}}). That suffices for an in-order receiver. A receiver that may see the final fragment first needs the position of every fragment and the total number of fragments from whichever fragment arrives first. Each fragment therefore carries an ascending index and the count. The index plays the role of the FCN; the count has no RFC 8724 counterpart.
* **Fixed deadline instead of Inactivity Timer.** The No-ACK receiver resets the Inactivity Timer on each Regular fragment (Section 8.4.1.2 of {{RFC8724}}). Here the deadline is fixed at the first arrival and never extended.
* **Frame ID and retirement record.** The Frame ID plays the role of DTag, sized so that a retired frame stays recognizable while late fragments can still arrive.

The mapping of the fields of this document to SCHC terms is as follows.

| Field | SCHC term | Status |
| --- | --- | --- |
| RuleID | RuleID | Open Issue: mapping to SCHC architecture unconfirmed. One byte, configured per peer association, value 0 in the prototype. |
| Frame ID | DTag (role) | Open Issue: mapping unconfirmed; width of 8 bytes is a prototype choice. |
| Fragment index | FCN (role) | Open Issue: mapping unconfirmed. |
| Fragment count | none | No RFC 8724 counterpart. |
| RCS | RCS | Open Issue: mapping unconfirmed. CRC-32C on the final fragment, in line with the integrity check of Sections 8.2.3 and 8.4.1 of {{RFC8724}}. |
{: #schc-mapping title="Mapping of fields to SCHC terms"}

RuleID comes first in the header, as in other SCHC messages, so that the format can later share a SCHC UDP port with other SCHC uses. A zero-bit RuleID is not used, because RFC 8724 leaves the RuleID size to the Profile and a whole byte preserves alignment.

The prototype wire format has no wire compatibility with RFC 8724. A deployment with a genuine in-order guarantee could instead specify a separate RFC 8724 No-ACK profile. Header compression is outside the scope of this document.
