# Manageability Considerations

This extension is configured, not negotiated. An implementation MUST provide configuration for the following items, and an operator is responsible for keeping both ends of each peer association consistent:

* the set of peer associations, each identifying the local tunnel endpoint, the peer address and the RuleID (0 in the prototype);
* the VNIs admitted on each peer association;
* the UDP destination port of the fragment format, which is locally configured in this version and distinct from the ordinary VXLAN port;
* the configured PMTU per peer association, from which the slice budget `C` is derived;
* the receiver limits: the discard deadline `Δ`, the arrival-spread bound `L` used for retirement records, the maximum number of fragments per frame (initially two), the maximum slice length, and the capacity of active and retired entries per scope.

Peer capability for the extension has to be provisioned before a sender emits oversized frames. How a peer's capability is learned is an Open Issue; this version relies on static provisioning.

An implementation SHOULD expose counters, kept per peer association and VNI where practical, for: frames fragmented; oversized frames dropped because the extension is unavailable or the limits cannot carry them; frames completed; entries expired with missing fragments; fragments received after expiry or retirement (late); duplicate fragments; integrity (RCS) failures; malformed input, including zero UDP checksums; peer or VNI admission failures; and table exhaustion. Expiry with missing fragments and late arrival are reported separately, as described in the discard deadline section.

# Security Considerations

This document defines no cryptographic protection. The first version targets statically provisioned unicast VTEP pairs in a managed fabric, and the considerations below assume that deployment.

## Peer and VNI Admission

A receiver MUST identify a configured peer association from the outer source address and the local tunnel endpoint, independently of the source UDP port, and MUST drop fragments from sources that match no association. A receiver MUST also drop fragments whose VNI is not admitted for that association. Drops at this stage occur before admission and create no reassembly state.

## Spoofing

Nothing in the fragment header authenticates the sender. A party able to inject packets with a forged outer source address can send fragments that appear to come from a configured peer, and can thereby inject frames into an admitted VNI or consume reassembly state. The CRC-32C RCS and the UDP checksum detect accidental corruption only. They are not keyed and provide no peer authentication or integrity against an attacker. A zero UDP checksum is dropped, which removes no spoofing exposure. Where spoofing is a concern, the deployment SHOULD use an authenticated underlay, such as an IPsec association or a fabric with source-address validation at its edges, or equivalent filtering that restricts the sources able to reach the configured port.

Because Frame IDs are a sequential counter, an attacker that can inject packets can open the next expected Frame ID with a wrong count, or send fragments that conflict with those of a genuine frame, so that the genuine frame is aborted. An unpredictable initial value only raises the cost of guessing, because the increments stay observable on the wire. The mitigation is the authenticated underlay described above.

## State Exhaustion

Reassembly state is the main resource an attacker can consume. The receiver bounds it by construction: entries are keyed by peer association, VNI, RuleID and Frame ID, the number of fragments per frame and the slice length are bounded, and every entry expires at `first_rx + Δ`, never extended. Because retirement records are kept for `L`, an attacker that opens many Frame IDs also occupies retirement capacity. A receiver SHOULD apply the capacity limits per scope so that one peer or VNI cannot exhaust the state of others, and when a scope is exhausted it MUST stop admitting new Frame IDs in that scope rather than evict state needed for duplicate suppression. Exhaustion is counted. Admission checks run before any state is created.

## Replay

Duplicate suppression through active and retired entries stops a captured frame from being delivered twice within the retention window `L`. It is not replay protection. A replayed fragment set whose Frame ID has left the retention window, or that arrives after a receiver restart without a fresh association generation or drain interval, can be accepted and delivered again. Restart-safe suppression is an Open Issue, tied to the Frame ID restart mechanism. Protection against an active replaying attacker requires an authenticated underlay with anti-replay.

## Other Considerations

A receiver never delivers a frame whose completing fragment arrived at or after expiry, so an attacker cannot extend the holding time by sending late fragments. An attacker that can delay or drop fragments can cause frames to be discarded at the deadline; this is loss, not corruption. Mixing fragments of different frames under a forged Frame ID is limited to the scope of one peer association and VNI, and a wrongly assembled frame fails the RCS check except with the probability of a CRC-32C collision, which an attacker can engineer deliberately.

# IANA Considerations

This document makes no request of IANA in this version. The UDP destination port of the fragment format is locally configured by the operator on both peers.

The SCHC UDP port of {{?I-D.ietf-schc-protocol-numbers}} is the candidate port for a later version. That draft has no assigned value yet and does not define the UDP payload layout used here, so it is cited informatively only. The following remain Open Issues:

* whether this format shares the SCHC UDP port, and how RuleID values are allocated on a shared port;
* the eventual UDP port assignment, if any, and the need for a header version indication; and
* whether any request to a SCHC registry is needed.
