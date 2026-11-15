---
title: "SCHC No-ACK Fragmentation of VXLAN Payloads"
abbrev: "SCHC VXLAN Fragmentation"
docname: draft-vladyka-schc-vxlan-fragmentation-00
category: std
submissiontype: IETF
ipr: trust200902
area: Internet
workgroup: SCHC
stand_alone: yes
pi: [toc, sortrefs, symrefs]
author:
  -
    name: Andrii Vladyka
    email: andrii.vladyka@gmail.com
  -
    name: Alexander Pelov
    organization: IMT Atlantique
    email: alexander.pelov@imt-atlantique.fr
normative:
  RFC7348:
  RFC8724:
informative:
  RFC8085:
  RFC8899:
  RFC4459:
  I-D.ietf-schc-protocol-numbers:
--- abstract

This document proposes a SCHC No-ACK profile for carrying VXLAN payloads, including those larger than the path MTU, over a managed UDP path. On a configured peer association every frame is carried as a SCHC Packet made of a NoCompress rule identifier, the uncompressed VXLAN header and the Ethernet frame. The packet is sent unfragmented when it fits the path MTU and fragmented otherwise. A four-bit fragment counter supports up to eight fragments. An optional receiver procedure for reordering is described separately because it changes the RFC 8724 No-ACK receiver behavior. The earlier indexed fragmentation proposal is retained in an appendix for comparison. The mapping and operational parameters require working group review.

--- middle

# Introduction

VXLAN {{RFC7348}} carries Ethernet frames over UDP. On a peer association configured for this profile, every frame is carried in this format on one configured UDP binding. A frame whose SCHC Packet fits the underlay IP path MTU (PMTU) is sent unfragmented; a larger one is sent as fragments in separate UDP datagrams. The receiver checks reassembly integrity before forwarding a reconstructed frame. A peer association not configured for this profile uses ordinary VXLAN unchanged. No acknowledgments or retransmissions are introduced. Unicast peer pairs in a managed fabric are the initial scope; broadcast, unknown unicast and multicast traffic can be handled by ingress replication to separately configured unicast peers.

The primary profile uses the No-ACK fragmentation and reassembly procedures of {{RFC8724}}. These procedures assume in-order delivery; a path and receiver processing that preserve order are therefore required for the ordered receiver. The sender transmits fragments in packet order. This document also describes an optional reordering-tolerant receiver extension that changes the treatment of All-1 and is not the unmodified No-ACK receiver procedure. Peer configuration determines which behavior is available. No inter-frame delivery order is guaranteed.

RFC 7348 Section 4.3 states that VTEPs MUST NOT fragment VXLAN packets. Here the input to SCHC is a VXLAN UDP payload and each fragment is carried in a separate UDP datagram on a distinct configured binding; IP fragmentation of an ordinary VXLAN packet is not used. Whether this distinction requires clarification of RFC 7348 is an open standards issue, not a claim that the question is resolved.

# Conventions and Terms

{::boilerplate bcp14-tagged}

A peer association identifies configured local and remote tunnel endpoints, direction, binding, RuleID, widths, timers and resource limits. PMTU is the underlay IP path MTU. A packet is the fragmented unit described below. The first-arrival deadline is an optional extension parameter distinct from the RFC 8724 inactivity timer. All integer and bit fields are transmitted most significant bit first.

# Binding and Profile

The sender forms a SCHC Packet consisting of an eight-bit NoCompress C/D RuleID, the eight-byte VXLAN header, and the complete Ethernet frame including carried VLAN tags and excluding FCS. This explicitly identified uncompressed packet follows the packet mapping of Sections 4, 5.1 and 7.2 of {{RFC8724}}. The C/D RuleID is configured at both ends, identifies the uncompressed VXLAN payload, and is distinct in function from the outer fragmentation RuleID. It appears once per packet, not once per fragment. After reassembly and RCS verification, the receiver checks and removes the C/D RuleID before VXLAN processing. Unknown C/D rules MUST be rejected. NoCompress support is mandatory and is the default; compression rules are outside this version. A SCHC Packet that fits the budget of the Sender Procedure is sent as one UDP payload without SCHC fragmentation; only a larger SCHC Packet is fragmented.

Unfragmented SCHC Packets and fragments use the same locally configured UDP destination port, distinct from ordinary VXLAN. Both peers MUST know the binding and profile before traffic is sent. There is no in-band negotiation or version field. The receiver selects exactly one configured association using the local IP address, local UDP destination port, remote IP address and direction, before parsing the RuleID. That association fixes the RuleID width and wire variant; source UDP port is not part of this lookup. Ambiguous configurations MUST be rejected. After the association is selected, a datagram whose first eight bits equal the configured NoCompress C/D RuleID is an unfragmented SCHC Packet; any other datagram is parsed as a fragment using the association's fragmentation RuleID width. The prefix-free assignment in the Fragment Format section makes this classification unambiguous. Multiple variants for the same peers require distinct local addresses or destination ports. The same outer source port and five-tuple SHOULD be used for all fragments of a packet to support flow affinity; the receiver identifies the peer independently of source UDP port. A sender MUST generate a UDP checksum for each fragment and the receiver MUST reject a zero or incorrect checksum, including over IPv4. Ordinary VXLAN behavior is unchanged on associations not configured for this profile; such an association MUST NOT carry a frame that exceeds the ordinary VXLAN payload budget.

Optional future compression requires independently specified matching rules and explicit context provisioning at both peers. It cannot be inferred from this profile. Rule mismatch can misroute traffic if VNI is elided; a future rule should preserve the VNI unless equivalent isolation is assured.

# Fragment Format

The No-ACK W field is absent. Each UDP payload carries a RuleID, DTag, four-bit FCN and fragment payload; the All-1 format also carries the 32-bit RCS immediately after the FCN, before its payload. Each fragment contains one nonempty tile. Field widths and bit order are as follows:

| Variant | RuleID | DTag | FCN | Regular header | All-1 header |
| --- | --- | --- | --- | --- | --- |
| Compact | 4 bits | 32 bits | 4 bits | 5 bytes | 9 bytes |
| Extended | 8 bits | 60 bits | 4 bits | 9 bytes | 13 bytes |
{: #tab-variants title="Configured wire variants"}

DTag size is configurable for each association and RuleID; additional sizes require the sum of RuleID, DTag and FCN widths to be a multiple of eight bits. Both ends MUST agree on all sizes and meanings before parsing. An existing RuleID mapping or association variant MUST NOT be changed until sender admission has stopped and the quarantine below has elapsed, including traffic not yet observed by the receiver. DTag width is selected by the already parsed fragmentation RuleID. For this binding, fragmentation and C/D RuleID assignments MUST be prefix-free within the configured context: an eight-bit C/D RuleID cannot begin with an assigned four-bit fragmentation RuleID, and in the extended variant the C/D RuleID MUST differ from every assigned eight-bit fragmentation RuleID. For example, compact fragmentation RuleID 1 and NoCompress C/D RuleID 0 are compatible. The default proposed variant is compact. The RuleID identifies the configured fragmentation rule; RuleID assignment is local to the association in this version. There is no W field or padding for these byte-aligned messages.

## FCN Values

The FCN is four bits, written `Fccc` for regular fragments. F identifies the first fragment. The three-bit c is the number of regular fragments remaining after this one, excluding All-1. For K fragments, 2 <= K <= 8, the first FCN is `1` followed by K-2; subsequent regular FCNs are `0` followed by the countdown from K-3 to zero. The final FCN is `1111`. Values `0110` and `0111` are invalid. The first regular tile contains the C/D RuleID and complete VXLAN header.

| K | FCNs in sender order |
| --- | --- |
| 2 | 1000, 1111 |
| 3 | 1001, 0000, 1111 |
| 4 | 1010, 0001, 0000, 1111 |
| 8 | 1110, 0101, 0100, 0011, 0010, 0001, 0000, 1111 |
{: #tab-fcn title="Example FCN sequences"}

RFC 8724 Section 8.4.1 permits a profile to set FCN width and define non-All-1 values. This encoding does not by itself change the RFC receiver's behavior.

## Reassembly Check Sequence

The RCS is CRC-32C over the entire packet in wire order, including its C/D RuleID and VXLAN header. The Castagnoli polynomial is 0x1edc6f41 (reflected 0x82f63b78); input and output are reflected, the initial register and final XOR are 0xffffffff, and the check value for ASCII `123456789` is 0xe3069283. Serialize the result most significant byte first. The profile chooses CRC-32C instead of the Ethernet CRC recommended in Section 8.2.3 of {{RFC8724}}. No padding is present for the byte-aligned binding described here. The UDP checksum and RCS detect errors but do not authenticate a sender.

# Sender Procedure

Let P be the configured IP PMTU and H the outer IP header size including options and any other encapsulation consuming the same PMTU budget. On an association configured for this profile, the SCHC Packet for an Ethernet frame of E bytes has E+9 bytes. If E+9 <= P-H-8, the sender MUST send the SCHC Packet unfragmented as one UDP payload. On an association not configured for this profile, the sender uses ordinary VXLAN if E+8 <= P-H-8 and otherwise drops and counts the frame.

Otherwise let A be the All-1 header size, 9 or 13 bytes, and set C=P-H-8-A. The sender MUST require C>=9 and compute K=ceil((E+9)/C). K is then at least 2. It MUST reject frames outside configured size limits or K outside 2..8. It snapshots C for the packet, divides the packet into K contiguous nonempty tiles, with every regular tile C bytes and the last tile at most C bytes, and transmits them in order. The unused four-byte RCS allowance in regular fragments keeps the sizing uniform. The IPv4 DF bit MUST be set and IPv6 source fragmentation MUST NOT be used. PMTU discovery is not specified; {{RFC8899}} describes a possible method. UDP operational considerations are discussed in {{RFC8085}}.

The sender allocates one DTag for the complete packet and uses the same RuleID and DTag on every fragment. No SCHC ACK, ACK REQ or retransmission is sent. This profile does not send Sender-Abort. Each association must be provisioned so its minimum supported PMTU and maximum frame length fit within eight fragments.

# Ordered Receiver

For the ordered receiver, Section 8.4.1 of {{RFC8724}} governs No-ACK processing. The receiver assembles regular fragment payloads in reception order and resets its inactivity timer on each regular fragment. On All-1 it appends the payload and checks the RCS; if the check fails it drops the packet, and reassembly concludes. On inactivity timer expiry it drops the incomplete packet. The timeout and RCS parameters are configured per rule. An All-1 arriving early is not held for missing fragments by this receiver.

The receiver MUST validate peer association, UDP checksum, rule and local resource bounds. It applies the structural validation below, but does not reorder regular tiles or suppress their duplicates within the RFC No-ACK reassembly procedure. Consequently duplicate regular fragments can cause an integrity failure. Terminal identifiers are subject to the retirement admission policy below. Before delivery it validates the reconstructed VXLAN header and authorizes the VNI for the peer. It learns MAC addresses only after successful reassembly and authorization. CRC checks cannot establish sender identity or prevent intentional collisions. A path that reorders fragments can cause loss even if all fragments arrive.

# Structural Validation and State Admission

Both receivers MUST validate the UDP checksum and association before accessing reassembly state. An unfragmented SCHC Packet, identified by its C/D RuleID, MUST be at least 23 bytes long (C/D RuleID, VXLAN header and an Ethernet header); a shorter one is malformed. It creates no reassembly state, uses no DTag and no retirement record, and is delivered after the VXLAN header is validated and the VNI authorized, as for a reassembled packet. As with ordinary VXLAN, duplicates of an unfragmented packet are not suppressed. Let h be the regular header length. Datagrams shorter than h are malformed. A header-only All-1 (exactly h bytes) is Sender-Abort under Section 8.3.4 of {{RFC8724}}; discard and retire any matching active entry without replying. It MUST NOT be interpreted as data. All-1 data requires at least h+5 bytes (four-byte RCS and one payload byte); lengths h+1 through h+4 are malformed. Regular data requires at least h+1 bytes. FCNs 6 and 7 are invalid. A first-marked regular fragment must contain at least nine payload bytes and the configured NoCompress C/D RuleID. Unknown rules, bad lengths, invalid FCNs and invalid checksums are discarded without modifying or allocating packet state.

Check retired identifiers before admission. Each admitted entry reserves capacity for its future retirement record. Enforce maximum retained packet bytes (maximum Ethernet frame length plus nine), maximum tile bytes, entry limits and per-peer/global orphan budgets before allocating or copying. Do not charge identical duplicates twice in the tolerant receiver. State that cannot be admitted is dropped; capacity MUST NOT be recovered by evicting a still-protected retirement record. On terminal completion, abort or timeout, release buffers and retain the identifier for R. A structurally valid fragment conflicting with admitted tolerant state terminates and retires that state; a malformed individual datagram does not.

# Optional Reordering-Tolerant Receiver

This section describes an experimental receiver extension, not unmodified RFC 8724 No-ACK. In particular, it may hold an early All-1 rather than immediately performing the integrity check and concluding reassembly. Its standards vehicle, including whether an RFC update or a separately specified mode is appropriate, remains open. A sender uses the same ordered sequence and cannot assume a receiver supports this extension without configuration.

The receiver keys state by peer association, direction, RuleID and DTag. Subject to per-peer and global entry and byte limits, any valid fragment may open an entry. An entry opened before the first fragment is an orphan and its VNI is not yet known. A first fragment reveals the C/D RuleID followed by the complete VXLAN header in the NoCompress binding; the receiver checks peer/VNI authorization and applies tenant accounting. It MUST reject unauthorized VNIs and MUST NOT learn MAC addresses before delivery.

The receiver stores each regular fragment by countdown and the final fragment separately. It ignores identical duplicates and aborts on conflicting duplicates. Identity compares the complete FCN and payload, and for All-1 also the RCS. A second first marker with a different countdown is a conflict; a first marker colliding with an already stored non-first position is also a conflict. An All-1 with unchanged payload but a changed RCS is a conflict. When the first marker arrives it supplies n=K-2; the expected regular positions are n down to zero, with the first marker only at n. Stored fragments outside these positions or with incompatible markers cause the entry to be discarded. Regular tiles have equal length, the final tile is no longer, and all bounds are checked before buffering. Completion requires the first fragment, all expected regular positions and All-1. The receiver assembles in countdown order, verifies the RCS once, validates the VXLAN unit and delivers once. On a complete packet with an incorrect RCS it discards immediately.

A fixed total deadline is measured from the first admitted fragment with a monotonic receive timestamp. A fragment arriving at or after the deadline is late. Duplicates do not extend it. A configured inactivity timer may expire sooner; regular arrivals reset that timer. The receiver discards incomplete state at the earlier expiry. Processing queues and batched timer sweeps MUST preserve the decision based on receive timestamps, including fragments received before the deadline but processed later. The deadline bounds reception of pieces, not end-to-end latency.

# DTag Reuse and Restart

DTag scope is the configured sending association and RuleID, across its VNIs and sender workers. Distinct overlapping packets MUST use distinct DTags as required by {{RFC8724}}. This proposal also requires a deployment-specific quarantine to prevent late duplicates from being associated with a new packet. Starting from zero on restart is safe only after that quarantine or with a distinguishable new association generation.

Define the following finite deployment bounds and configure them at both endpoints:

* S: maximum interval from first to last emission of any fragment copy of a packet. The sender MUST stop emitting the packet when S is reached; queued work cannot resume that packet later.
* B: maximum interval from emission of a copy to its receiver admission timestamp, including network duplication and receive queues. A later locally emitted copy is also subject to S. Let W=S+B, which conservatively bounds the arrival spread.
* I: configured ordered-receiver inactivity timeout. For valid traffic, its maximum entry lifetime is D_ordered=W+I: the final regular arrival occurs within W of first arrival and can restart I.
* Delta: tolerant receiver total deadline. Its maximum entry lifetime is D_tolerant=Delta, irrespective of inactivity resets.
* D: the applicable D_ordered or D_tolerant; when behavior is not fixed, use their maximum.
* R: retirement retention, at least W, including timing margin.

These bounds apply to all copies, including delayed originals after an early timeout. Retaining an identifier for B alone is insufficient: the sender may still be emitting fragments after the receiver has retired it. Retention for W covers that remaining emission span and transport delay. Refreshing retirement on late copies is unnecessary under the stated bounds.

The sender MUST NOT reuse a DTag until Q > B+D+R (including timing margin) after its previous last emission. B+D bounds how late old state may terminate; R then allows its retirement record to expire. If that last-emission time is unknown after restart, wait Q from startup. Packet-rate and width limits MUST prevent wrap before Q; otherwise stop admission. All sender workers and VNIs in the association share allocation. Persistent counter reservations or a receiver-visible generation may avoid restart waiting but are not encoded in this format.

An ordered receiver has no fixed total deadline in RFC 8724; D_ordered is a bound derived from compliant finite emission and delivery, not a new timer that changes its state machine. Repeated hostile traffic is outside that assumption and requires resource controls and authentication. A tolerant receiver uses its actual fixed deadline. Configure I and Delta against W and the desired discard budget; no numeric defaults are implied.

After receiver restart, stop sender admission and drain for W from confirmed cessation of new packet starts before reopening the same association, allowing already started packets their remaining S. Alternatively, after confirmed cessation of all emissions, drain for B. A new generation that rejects old traffic is another option. Blindly waiting B from receiver boot while a sender continues old work is insufficient. These bounds require coordinated configuration; absent defensible limits the suppression guarantee is best effort. Authenticated anti-replay protection is required against intentional replay.

# Examples

With IPv4 without options, P=1500 and H=20, an Ethernet frame of up to 1463 bytes is sent unfragmented, one byte less than ordinary VXLAN carries. With compact headers, C=1463. An untagged 1514-byte Ethernet frame yields a 1523-byte SCHC Packet (one C/D RuleID byte plus a 1522-byte VXLAN payload), split into 1463 and 60 bytes. FCNs are `1000` and `1111`, and outer IP lengths are 1496 and 97 bytes. For a 9014-byte frame, seven fragments suffice. With IPv6 and P=1280, H=40, C=1223 and the same 9014-byte frame requires eight fragments. With IPv4, P=1280 and H=20, a frame of up to 1243 bytes is sent unfragmented and a 1514-byte frame becomes a 1523-byte SCHC Packet split into compact fragments of 1243 and 280 bytes. These figures exclude additional encapsulation and require corresponding configured frame limits.

A four-fragment packet has FCNs `1010`, `0001`, `0000`, `1111`. A tolerant receiver may receive `1111`, `0000`, `1010`, `0001`: it holds the final fragment, learns the expected positions from `1010`, and verifies the RCS after `0001` arrives. An ordered RFC 8724 receiver instead concludes its processing on receipt of the early All-1.

# Implementation and Verification Status

The accompanying `check_vectors.py` is an executable wire codec and complete-set reassembly checker for this revision. It verifies the classification and minimum length of unfragmented SCHC Packets, the boundary between unfragmented and fragmented sending, all arrival permutations for K=2..8, missing positions, duplicate and corruption handling, malformed lengths, both header layouts and MTU examples. `wire-vectors.json` contains complete fragment UDP payloads for both variants. It is not a timed receiver or a production implementation: timer races, admission limits, restart coordination and lifetime bounds still require implementation testing and independent review. The older Python reference model applies only to the historical appendix.

# Security Considerations

Only configured peers should be admitted. Bound orphan state, total incomplete packets, per-peer and per-tenant bytes, and retirement records. A VNI is unavailable until the first fragment, so resources used by earlier arrivals cannot be charged to a tenant at admission. A sender can exhaust resources or forge headers; checksums and CRC do not prevent these attacks. Use authenticated transport and anti-replay where the threat model requires them. A receiver must enforce VNI authorization after it learns the VNI and again before delivery as appropriate. Configuration errors, mismatched rules or reuse of identifiers can cause loss or cross-packet mixing.

# IANA Considerations

This version requests no IANA action. UDP port assignment, shared SCHC port demultiplexing, RuleID allocation, and any registry implications remain open.

# Open Issues

* Review the proposed NoCompress VXLAN binding and prefix-free rule provisioning with the working group.
* Determine the standards vehicle for the optional tolerant receiver and its relationship to RFC 8724 and RFC 7348.
* Specify capability provisioning, UDP demultiplexing and any IANA requests.
* Set interoperable timer, packet lifetime, memory and frame-size parameters; validate DTag width against actual rate and restart requirements.
* Define and verify any optional compression rules separately.

# Acknowledgements
{:numbered="false"}

Thanks to Laurent Toutain for the useful discussions and the inputs regarding NoAck.

--- back

# Earlier Indexed Out-of-Order Proposal

This appendix is informative and preserves the earlier draft's technical sections for comparison. Its capitalized requirements describe only the historical alternative, not conformance requirements for the principal profile. Statements that an out-of-order receiver requires a total count in every fragment are superseded by the first-marker/countdown construction in the main text. Historical port-assignment claims below are not current allocation guidance. Its wire format and procedures are an alternative, not part of the main profile above; statements within this appendix describe that earlier proposal. The existing reference model and generated examples apply to this appendix only. The original standalone source remains available separately.


## Introduction

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

## Terminology

The key words in this appendix have the meanings defined in the main text.

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

## Placement and Interoperability

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

## Relationship to SCHC

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

## Fragment Format

This section defines the UDP payload of each fragment. The wire values are provisional prototype choices. The format has no wire compatibility with {{RFC8724}}.

All multibyte integers use network byte order. The UDP destination port, configured locally, selects this format. The RuleID comes first, as in other SCHC messages, so that the format can later share a SCHC UDP port with other SCHC uses and be selected by RuleID.

### Header

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

### Mode Fields and Binding Fields

The Out-of-Order No-ACK mode proper consists of the RuleID, the Frame ID, the fragment index, the fragment count, the slice and the RCS, together with the receiver procedure and the discard deadline. The UDP/Ethernet binding for VXLAN consists of the VNI, the UDP destination port and UDP checksum, the use of an Ethernet frame without FCS as the fragmented unit, and the PMTU-derived slice budget of the sender procedure. The binding is kept explicit so that the mode can later be separated from it.

Open Issue: whether the mode and the binding remain in one document is not settled.

### Reassembly Check Sequence

The final fragment, and only the final fragment, carries a 4-byte RCS after its slice. It is the CRC-32C of the original frame bytes in wire order, serialized big-endian. The RCS guards against splicing slices of different frames, for example after a restart. It covers the frame bytes only and checks reassembly integrity; it does not protect the header.

CRC-32C is defined as follows:

* Polynomial: the Castagnoli polynomial, 0x1edc6f41 (0x82f63b78 in reflected form).
* Input and output are reflected.
* The initial register value is 0xffffffff.
* The final XOR value is 0xffffffff.
* The check value for the ASCII string `123456789` is 0xe3069283.
* The result is serialized as four bytes, most significant byte first.

### UDP Checksum

A sender MUST generate the UDP checksum on every extension datagram, over IPv4 as well as IPv6. A receiver MUST drop an extension datagram whose UDP checksum is zero or incorrect. This differs from Section 5 of {{RFC7348}}, which recommends a zero UDP checksum for VXLAN. Here the UDP checksum is the only protection of the fragment header.

### UDP Port Selection

The UDP destination port is selected by configuration and identifies this format, distinct from the port used for ordinary VXLAN. Each sender is configured with the destination port on which its peer receives this format.

Historical open issue: this alternative used a locally configured UDP port and did not settle shared-port demultiplexing, version indication or RuleID allocation. The current status and applicability of {{I-D.ietf-schc-protocol-numbers}} must be checked separately; this appendix makes no claim about its current allocations.

### Provisional Status

The field sizes, offsets and the RuleID value in this section are provisional prototype choices and may change before any interoperable specification. A reference model implements exactly this layout (historical reference model).

## Sender Procedure

The PMTU used here is the underlay IP path MTU, excluding outer Ethernet overhead. `H_IP` is the length of the actual outer IP header, including any options or extension headers, and any further encapsulation that consumes the same PMTU budget.

PMTU is a sender configuration value per peer and per direction. This document defines no default PMTU; the value 1500 appears only in examples. The receiver does not need the sender's PMTU and MUST NOT assume a slice length.

The ordinary VXLAN frame budget is:

~~~
PMTU - H_IP - 8 (UDP) - 8 (VXLAN)
~~~

The slice budget `C` is the same for every fragment of a frame:

~~~
C = PMTU - H_IP - 8 (UDP) - 14 (fragment header) - 4 (RCS allowance)
~~~

Nonfinal datagrams therefore leave four bytes unused, which removes a sizing special case for the final fragment.

For each Ethernet frame (as presented to the tunnel, without FCS) the sender proceeds as follows.

1. If the frame fits the ordinary VXLAN frame budget, it emits RFC 7348 VXLAN.
2. Otherwise, if the peer is not configured for this extension, it drops the frame and increments a local counter.
3. It validates `C > 0`, computes `N = ceil(frame_length / C)`, and drops the frame if the length is outside the configured frame-length range or `N` is outside the configured fragment-count range (2 through the configured maximum).
4. It takes a snapshot of the budget once per frame, so a PMTU change affects only later frames.
5. It sends the slices in ascending index order. Every slice has `C` bytes except the final one, which carries the remainder followed by the 4-byte RCS. Every outer IP datagram fits the PMTU.

A configuration MUST be rejected if the minimum configured PMTU cannot carry the maximum frame length in the maximum number of fragments.

All fragments of a frame use the same outer five-tuple, in particular the same source UDP port. The sender derives that port from the inner frame as it does for ordinary VXLAN ({{RFC7348}}, Section 5), to preserve flow-hash affinity. The receiver identifies the peer independently of the source UDP port, and the protocol tolerates reordering even when path affinity fails.

For IPv4 the sender sets DF. For IPv6 the sender does not use source fragmentation.

Worked numbers with PMTU 1500, no outer IP options or extension headers: standard VXLAN carries complete Ethernet frames of up to 1464 bytes over IPv4 (`H_IP` = 20) and 1444 bytes over IPv6 (`H_IP` = 40). The slice budgets are 1454 and 1434 bytes. Over IPv4, a 1514-byte frame produces slices of 1454 and 60 bytes; the outer IP lengths are 1496 and 106 bytes (the final datagram carries 60 + 14 + 4 + 8 + 20).

The first version uses a conservative static PMTU per peer and path. Open Issue: dynamic discovery is not specified. If it is specified later, it has to describe how the extension probes its own path, including ECMP and path changes; see RFC 8899 {{RFC8899}}.

## Receiver Procedure

The wire format carries no original-length field. The receiver relies on the fragment count, on equal-length nonfinal slices, on the configured maximum frame length and on the RCS. It does not assume a slice length.

An entry is keyed by the configured peer association (including the local tunnel endpoint), VNI, RuleID and Frame ID. Because the RuleID of an association is configured and a mismatch is dropped, it does not distinguish entries within an association. A single worker or equivalent synchronization owns each entry. The receiver uses a local monotonic clock for admission, expiry and delivery decisions.

1. **Validation before allocation.** Before allocating state the receiver MUST check that: the peer and VNI are authorized; the UDP checksum is nonzero and correct; the RuleID equals the value configured for the peer association (a mismatch is counted as a peer admission failure); the count is at least 2 and at most the configured maximum; the index is less than the count; the slice length does not exceed the receiver's configured maximum slice length; and a nonempty slice remains after the header and any required trailer, so that a final fragment carries at least one slice byte and the 4-byte RCS. The maximum slice length is a local bound, at most the configured maximum frame length. A datagram that fails the checksum or bounds checks is dropped and counted as malformed. Caps on incomplete entries and retained bytes apply per peer and globally. If admitting a fragment would exceed a cap, the fragment is dropped and counted as table exhaustion; an existing entry stays pending until its deadline.
2. **Admission and expiry.** A fragment for a retired ID, whether the frame completed, aborted or expired, is dropped and counted as late. The first admissible fragment, of any index, opens an entry with `expiry = first_rx + Δ`; the deadline is never extended. Expiry is decided by the arrival timestamp `t` alone, taken at a consistent receive stage. Expired entries are ineligible for completion even if no timer sweep has run: a fragment that finds an expired entry retires it as expired (counted once) and is itself counted as late. Timer processing can be batched.
3. **Consistency and duplicates.** The count MUST match across the entry, every nonfinal slice MUST have the same length, and the final slice MUST be no longer than a nonfinal slice. The receiver stores one slice per index and updates a bitmap and a running byte count. An identical duplicate, meaning a fragment with the same index and the same slice and, for the final fragment, the same RCS, is ignored and does not extend expiry. A fragment with the same index but a different slice or, for the final fragment, a different RCS is a conflicting duplicate. A conflicting duplicate, inconsistent metadata, or a byte count above the maximum frame length aborts and retires the entry.
4. **Completion.** When the bitmap is complete, the sum of the slice lengths MUST lie within the configured frame-length range. The receiver assembles the slices in index order, verifies the RCS and delivers the frame once. The frame is eligible if the arrival timestamp of the completing fragment is before expiry; assembly and verification time after that arrival is budgeted in `Δ`. A full bitmap with an invalid length or RCS aborts the entry.
5. **Retirement.** On completion, abort or timeout the receiver retires the ID and releases the buffers. A final fragment with holes stays pending until the holes fill or the deadline passes.

Open Issue: the deadline equality rule. The reference receiver treats `t >= expiry` as expired, so a fragment arriving exactly at the deadline is late. This is a recommendation pending working group input.

Because an entry may start from any index, a fragment of a retired frame can arrive after retirement and would otherwise open a new entry. A bounded retirement record suppresses such late fragments and duplicate delivery. A record MUST NOT be evicted while the deployment still allows fragments for that ID to arrive. For the first version, `L` is configured as an upper bound on the arrival spread of all copies of the fragments of a frame, including duplicates, and a record is kept for `L` after retirement. Capacity for active and retired IDs is either provisioned for the admitted rate, or a retirement slot is reserved at admission, as the reference receiver does; new IDs are then not admitted within an exhausted scope until space returns. Drops before admission create no deliverable state. Without a defensible bound on `L`, suppression through finite retention is best effort.

Starting from any index supports reordering because every fragment carries its own index and the count, so the first fragment to arrive, including the final one, is enough to open an entry and size its bitmap, and missing indices simply stay pending.

The receiver maintains counters for: completed frames, expiration, late fragments, duplicates, integrity failure (inconsistent metadata, conflicting duplicates, bad length or RCS), malformed input, peer admission failure, and table exhaustion. Expiration and lateness are reported separately: expiration records incompleteness observed at the cutoff, while a late fragment, meaning any fragment of a retired Frame ID, is positive evidence of lateness. A duplicate is an identical fragment received while its entry is still active; an identical fragment arriving after retirement is late, not a duplicate.

## Discard Deadline

A receiver can observe incompleteness only at the cutoff. At that moment it cannot tell whether a missing fragment was lost or will arrive later. A fragment received after expiry is positive evidence of lateness. An implementation SHOULD report the two events separately: an expired entry with missing fragments (incompleteness, loss or lateness not distinguished) and a fragment arriving after expiry or retirement (late).

The deadline is `expiry = first_rx + Δ`, fixed at the first admissible fragment and never extended. The prototype rule, subject to the Open Issue below, is that an entry is expired for any fragment whose arrival timestamp `t` satisfies `t >= expiry`.

### Choosing Δ

For a deterministic path, `Δ` is budgeted from three terms:

* the maximum sender emission span for the fragments of one frame;
* the maximum differential path delay between fragments of the same frame; and
* the maximum receiver scheduling, processing and clock-resolution margin, which includes assembly and RCS verification after the completing fragment arrives.

An absolute maximum one-way latency can inform the differential bound, but it is the spread between pieces of the same frame that drives this timer.

For a best-effort path, the operator measures the spread under realistic contention and chooses the cutoff against the service's latency and discard budgets. The observation stage at which arrival timestamps are taken is defined when benchmarking, and MUST be applied consistently within a receiver.

### Scope of the Deadline

The first-arrival deadline bounds only the time the receiver holds a frame for reassembly. It is not an end-to-end frame age bound. An end-to-end bound requires a separate age mechanism, such as sender timestamps with bounded clock error, which is outside the base format of this document.

## Frame Identification and Restart

The Frame ID is a 64-bit counter. The sender uses one monotonic counter per sending association and VNI, and every frame fragmented in that scope takes the next value. A Frame ID MUST be unique across active and recently retired frames within the configured peer association and VNI. The counter starts at any value chosen by the sender. At 64 bits, wrapping is not expected within the lifetime of a deployment, and behaviour at wrap is not specified. The 8-byte width is a prototype choice kept for alignment; the width itself is an Open Issue.

### Sender Restart

A sender restart must not result in the reuse of a Frame ID that a receiver may still hold as active or retired. This is a requirement on the deployment; the mechanism is an Open Issue. Two approaches are candidates, and neither is specified in this version:

* it persists counter allocation, so that after a restart the counter resumes above every value previously used; or
* it establishes a new association generation that the receiver can distinguish, and the receiver rejects the old generation.

### Receiver Restart

A receiver that loses its retirement records can accept late or duplicate fragments of frames it already delivered or retired. If duplicate suppression must survive a receiver restart, the receiver needs either a fresh association or a drain interval before it reopens the old association. The drain interval covers the residual packet lifetime of the deployment.

The CRC-32C RCS provides a partial safeguard: it guards against splicing slices of different frames, for example after a restart. It does not replace Frame ID uniqueness.

### Status

The prototype puts no epoch or generation value on the wire. Restart-safe interoperability is not claimed.

Open Issue: the restart and generation mechanism is not specified. It covers how a new association generation is signalled and distinguished, how the old generation is rejected, how counter persistence is performed and the length of the receiver drain interval. Until it is specified, this document makes no claim of restart-safe interoperability.

### Retirement Bound L

Frame ID uniqueness depends on the retirement bound `L`. The retirement record of a frame is kept for `L` after retirement. In the first prototype, `L` is configured as an upper bound on the arrival spread of all copies of the fragments of a frame, including duplicates. A receiver MUST NOT evict a retirement record while the deployment still allows fragments for that Frame ID to arrive. Without a defensible bound, suppression through finite retention is best effort.

## Default Operational Profile

This section describes the prototype operating point. It is not a claim of general suitability.

### Fragment Count

The profile starts with a configured maximum of two fragments. This is valid where the minimum configured PMTU carries the maximum allowed frame in two slices, for example an Ethernet-1500 service over a 1500-byte PMTU. The receiver can then keep a two-bit receive mask and bounded per-worker state.

### Minimum-PMTU Validation

A configuration MUST be rejected if the minimum configured PMTU cannot carry the maximum frame length in the maximum number of fragments. Validation uses the slice budget `C` for each configured peer and direction, with the actual `H_IP`.

### Configured Bounds

An implementation provides the following configured values:

* the maximum frame length, and the frame-length range accepted;
* the maximum fragment count (2 in the profile above), and the minimum count of 2;
* the receiver's maximum slice length, a local bound that does not exceed the maximum frame length;
* caps on incomplete entries and retained bytes per peer and globally; and
* capacity for retirement records at the admitted rate, or a reservation of a retirement slot at admission with admission stopped within an exhausted scope until space returns.

### Meaning and Configuration of Δ and L

`Δ` is the receiver-held reassembly time measured from the first admissible fragment, chosen as described above. `L` is the retention time of a retirement record, measured from retirement, bounding the arrival spread of all copies of a frame. Both are local receiver configuration. Neither value is carried on the wire.

Open Issue: this document gives no numeric default for `Δ` or `L`, for the maximum frame length, or for the entry and byte caps. The values depend on the path and on measurement. They are not set here.

Open Issue: the deadline equality rule is that the prototype treats `t >= expiry` as expired, so a fragment arriving exactly at the deadline is late. Whether this is the final rule is to be confirmed.

## Manageability Considerations

This extension is configured, not negotiated. An implementation MUST provide configuration for the following items, and an operator is responsible for keeping both ends of each peer association consistent:

* the set of peer associations, each identifying the local tunnel endpoint, the peer address and the RuleID (0 in the prototype);
* the VNIs admitted on each peer association;
* the UDP destination port of the fragment format, which is locally configured in this version and distinct from the ordinary VXLAN port;
* the configured PMTU per peer association, from which the slice budget `C` is derived;
* the receiver limits: the discard deadline `Δ`, the arrival-spread bound `L` used for retirement records, the maximum number of fragments per frame (initially two), the maximum slice length, and the capacity of active and retired entries per scope.

Peer capability for the extension has to be provisioned before a sender emits oversized frames. How a peer's capability is learned is an Open Issue; this version relies on static provisioning.

An implementation SHOULD expose counters, kept per peer association and VNI where practical, for: frames fragmented; oversized frames dropped because the extension is unavailable or the limits cannot carry them; frames completed; entries expired with missing fragments; fragments received after expiry or retirement (late); duplicate fragments; integrity (RCS) failures; malformed input, including zero UDP checksums; peer or VNI admission failures; and table exhaustion. Expiry with missing fragments and late arrival are reported separately, as described in the discard deadline section.

## Security Considerations

This document defines no cryptographic protection. The first version targets statically provisioned unicast VTEP pairs in a managed fabric, and the considerations below assume that deployment.

### Peer and VNI Admission

A receiver MUST identify a configured peer association from the outer source address and the local tunnel endpoint, independently of the source UDP port, and MUST drop fragments from sources that match no association. A receiver MUST also drop fragments whose VNI is not admitted for that association. Drops at this stage occur before admission and create no reassembly state.

### Spoofing

Nothing in the fragment header authenticates the sender. A party able to inject packets with a forged outer source address can send fragments that appear to come from a configured peer, and can thereby inject frames into an admitted VNI or consume reassembly state. The CRC-32C RCS and the UDP checksum detect accidental corruption only. They are not keyed and provide no peer authentication or integrity against an attacker. A zero UDP checksum is dropped, which removes no spoofing exposure. Where spoofing is a concern, the deployment SHOULD use an authenticated underlay, such as an IPsec association or a fabric with source-address validation at its edges, or equivalent filtering that restricts the sources able to reach the configured port.

Because Frame IDs are a sequential counter, an attacker that can inject packets can open the next expected Frame ID with a wrong count, or send fragments that conflict with those of a genuine frame, so that the genuine frame is aborted. An unpredictable initial value only raises the cost of guessing, because the increments stay observable on the wire. The mitigation is the authenticated underlay described above.

### State Exhaustion

Reassembly state is the main resource an attacker can consume. The receiver bounds it by construction: entries are keyed by peer association, VNI, RuleID and Frame ID, the number of fragments per frame and the slice length are bounded, and every entry expires at `first_rx + Δ`, never extended. Because retirement records are kept for `L`, an attacker that opens many Frame IDs also occupies retirement capacity. A receiver SHOULD apply the capacity limits per scope so that one peer or VNI cannot exhaust the state of others, and when a scope is exhausted it MUST stop admitting new Frame IDs in that scope rather than evict state needed for duplicate suppression. Exhaustion is counted. Admission checks run before any state is created.

### Replay

Duplicate suppression through active and retired entries stops a captured frame from being delivered twice within the retention window `L`. It is not replay protection. A replayed fragment set whose Frame ID has left the retention window, or that arrives after a receiver restart without a fresh association generation or drain interval, can be accepted and delivered again. Restart-safe suppression is an Open Issue, tied to the Frame ID restart mechanism. Protection against an active replaying attacker requires an authenticated underlay with anti-replay.

### Other Considerations

A receiver never delivers a frame whose completing fragment arrived at or after expiry, so an attacker cannot extend the holding time by sending late fragments. An attacker that can delay or drop fragments can cause frames to be discarded at the deadline; this is loss, not corruption. Mixing fragments of different frames under a forged Frame ID is limited to the scope of one peer association and VNI, and a wrongly assembled frame fails the RCS check except with the probability of a CRC-32C collision, which an attacker can engineer deliberately.

## IANA Considerations

This document makes no request of IANA in this version. The UDP destination port of the fragment format is locally configured by the operator on both peers.

The SCHC UDP port of {{?I-D.ietf-schc-protocol-numbers}} is the candidate port for a later version. This historical alternative did not settle its use; the reference is informative and does not assert current allocation status. The following remain Open Issues:

* whether this format shares the SCHC UDP port, and how RuleID values are allocated on a shared port;
* the eventual UDP port assignment, if any, and the need for a header version indication; and
* whether any request to a SCHC registry is needed.

## Examples

Both examples use the following parameters, produced by the historical reference model.

* RuleID 0 (prototype value); VNI 12345 (0x003039); Frame ID 1 (0x0000000000000001); peer vtep-2.
* PMTU 1500 bytes, IPv4, H_IP = 20 bytes (no outer IP options).
* Illustrative Ethernet frame of 1514 bytes, with byte\[i\] = i mod 256.
* Deadline Δ = 5.0 ms; retirement record lifetime L = 50.0 ms.

The sender produces two fragments. CRC-32C over the 1514-byte frame is 0x487a7b87.

| Index | Count | Slice length | Outer IP length | Header fields (hex) | RCS (hex) |
| --- | --- | --- | --- | --- | --- |
| 0 | 2 | 1454 | 1496 | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x00 Count=0x02 | — |
| 1 | 2 | 60 | 106 | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x01 Count=0x02 | 487a7b87 |

### Example A: Last-Fragment-First Completion

The last fragment (index 1, carrying the RCS) arrives before the nonfinal fragment (index 0). The receiver opens the entry on the last fragment's arrival and delivers once the bitmap is complete and the RCS verifies.

| t | Event | Header fields (hex) | Result | Bitmap after datagram |
| --- | --- | --- | --- | --- |
| 0.0 ms | final fragment (index 1) arrives first | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x01 Count=0x02 | PENDING | 01 |
| 1.0 ms | nonfinal fragment (index 0) arrives second, before expiry at 5.0 ms | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x00 Count=0x02 | DELIVERED | 11 |

Delivered frame length: 1514 bytes; CRC-32C: 0x487a7b87.
Counters after Example A: completed=1, expired=0, late=0, duplicate=0, integrity_failure=0, malformed=0, peer_admission_failure=0, table_exhaustion=0.

### Example B: Deadline Expiry

Only the last fragment (index 1) arrives. The entry opens with expiry = first_rx + Δ = 0.0 + 5.0 = 5.0 ms. No sweep runs. The missing nonfinal fragment (index 0) then arrives after expiry: the entry is ineligible for completion even though no sweep has retired it yet, so it is retired as expired on this arrival and the arriving fragment is itself counted as late.

| t | Event | Header fields (hex) | Result | Bitmap after datagram |
| --- | --- | --- | --- | --- |
| 0.0 ms | final fragment (index 1) arrives alone; entry opens, expiry = 0.0 + Δ = 5.0 ms | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x01 Count=0x02 | PENDING | 01 |
| 7.0 ms | missing nonfinal fragment (index 0) arrives after expiry; the entry is retired as expired and the fragment is counted as late | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x00 Count=0x02 | LATE | 01 |

Counters after Example B: completed=0, expired=1, late=1, duplicate=0, integrity_failure=0, malformed=0, peer_admission_failure=0, table_exhaustion=0.

## Test and Benchmark Considerations

An implementation can start with a maximum of two fragments and a two-bit receive mask, and should benchmark the integrity check and the fragmented forwarding path together. Throughput results are pending measurement; this document makes no throughput claim. Acceptance checks include:

* Exact-fit and one-byte-over frame sizes for IPv4 and IPv6, including tagged Ethernet, verifying that every emitted IP packet fits the PMTU.
* Both arrival orders, every single-fragment loss, identical and conflicting duplicates, malformed or inconsistent headers, bad RCS, deadline equality, late arrival after retirement, and Frame ID uniqueness across restart.
* Normal VXLAN forwarding, unsupported peers, ingress replication, table exhaustion, and a change of configured PMTU.
* Benchmarks of a normal/fragmented mix, of frames just over the VXLAN threshold, and of the maximum frame size, reporting frame and packet rates, wire bit rate, latency percentiles and drops by cause, and stating whether offered load is ingress frame rate or egress wire capacity.
* Counters for completed frames, expiration, late fragments, duplicates, integrity failure, malformed input, peer admission failure, and table exhaustion.
