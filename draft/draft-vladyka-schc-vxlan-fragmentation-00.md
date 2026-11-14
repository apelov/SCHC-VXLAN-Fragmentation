---
title: "Deadline-Bounded Ethernet Fragmentation for VXLAN using SCHC Out-of-Order No-ACK"
abbrev: "SCHC VXLAN Fragmentation"
docname: draft-vladyka-schc-vxlan-fragmentation-00
category: std
submissiontype: IETF
ipr: trust200902
area: Internet
workgroup: SCHC
keyword:
  - SCHC
  - VXLAN
  - fragmentation
  - Ethernet
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
  RFC2119:
  RFC8174:
  RFC7348:
  RFC8724:
  RFC8899:

informative:
  I-D.ietf-schc-protocol-numbers:

--- abstract

This document specifies a deadline-bounded fragmentation mode for Ethernet frames carried over VXLAN between a pair of VTEPs. A frame that exceeds the underlay path MTU is split into slices carried in separate UDP datagrams, and is delivered by the receiver only after successful reassembly. The mode uses no acknowledgments and no retransmissions, bounds receiver state and waiting time, and tolerates fragment reordering. It is defined as a SCHC fragmentation mode named Out-of-Order No-ACK. Several parameters and mappings are marked as open issues in this version.

--- middle

{::include sections/01-introduction.md}

{::include sections/02-wire-format.md}

{::include sections/03-procedures.md}

{::include sections/04-timing-and-profile.md}

{::include sections/05-security-iana.md}

{::include sections/06-examples.md}

# Open Issues Summary

This section collects the Open Issues stated in the preceding sections. Each remains open in this version.

* Placement (Introduction): UDP demultiplexing, header versioning, any IANA request, and peer capability provisioning before oversized frames are sent.
* Relationship to SCHC (Introduction): how the Out-of-Order No-ACK mode integrates with the SCHC framework, and the mapping of RuleID, Frame ID, fragment index and RCS to SCHC architecture terms, including the width of the Frame ID.
* Fragment Format: header versioning and UDP demultiplexing, including whether the SCHC UDP port is shared, the allocation of RuleID values on a shared port, and any IANA request.
* Sender Procedure: dynamic PMTU discovery, which is not specified.
* Receiver Procedure and Default Operational Profile: the deadline equality rule, that is, whether a fragment arriving exactly at the deadline is late.
* Frame Identification and Restart: the restart and generation mechanism, including signalling and rejecting a generation, counter persistence and the receiver drain interval; the 8-byte Frame ID width is also open.
* Default Operational Profile: numeric defaults for the discard deadline and retirement bound, the maximum frame length, and the entry and byte caps.
* Security Considerations: how peer capability is learned, and restart-safe duplicate and replay suppression.
* IANA Considerations: sharing of the SCHC UDP port, the eventual port assignment and header version indication, and whether any request to a SCHC registry is needed.

--- back

# Acknowledgments
{:numbered="false"}

The authors thank the members of the SCHC working group for their discussions of fragmentation over tunnels.
