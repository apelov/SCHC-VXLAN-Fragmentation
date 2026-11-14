# Durable Decisions Record

This file records durable architectural decisions made for the project.
Append decisions using `python3 agentwork/pipeline.py decide --title "<Title>" --text "<Summary>"`.

---

## 2026-11-14: Short first version — wire format revisions

- CRC-32C RCS kept (it is the SCHC integrity check, RFC 8724 §8.2.3/§8.4.1).
- Original-frame-length field removed; header is 14 bytes (VNI 3, RuleID 1, Frame ID 8, index 1, count 1).
- RuleID stays one byte (value 0) for alignment.
- PMTU is a per-peer, per-direction sender configuration, no default; 1500 only in examples.
- Frame ID stays 8 bytes; width is an open item.
- Slice budgets 1454 (IPv4) / 1434 (IPv6); a 1514-byte frame gives slices 1454 + 60, outer IP lengths 1496 / 106.
- Draft has six sections (introduction, wire format, procedures, timing and profile, security/IANA, examples).

## 2026-11-14: Design review, RuleID first, UDP port

- Index plus count kept instead of an FCN; expiry decided by arrival timestamp (`t >= expiry` expired); zero UDP checksums dropped; source port derived as for VXLAN.
- RuleID moved to byte 0 (VNI bytes 1-3), configured per peer, 0 in the prototype.
- UDP port stays locally configured; the SCHC port of draft-ietf-schc-protocol-numbers is the candidate once assigned (open item). NAT is outside the target deployment.

## 2026-11-14: Mode name and category

- The mode is named Out-of-Order No-ACK, a new mode, not an extension of No-ACK.
- The draft is Standards Track (`category: std`).
