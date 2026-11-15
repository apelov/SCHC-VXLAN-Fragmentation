//! Sender Procedure: decide whether a frame's SCHC Packet is sent
//! unfragmented or split into K tiles, and build the resulting UDP
//! payload(s).

use crate::crc32c::crc32c;
use crate::wire::{encode, fcn_sequence, Variant, WireError, CD_RULE};

/// Per-association sender configuration (Sender Procedure section).
#[derive(Clone, Copy, Debug)]
pub struct SendConfig {
    pub variant: Variant,
    /// P: configured IP PMTU.
    pub pmtu: usize,
    /// H: outer IP header size, including options and any further
    /// encapsulation consuming the same PMTU budget.
    pub h_ip: usize,
    /// The association's configured fragmentation RuleID.
    pub fr_rule: u64,
}

/// Build the SCHC Packet for one Ethernet frame: an eight-bit NoCompress
/// C/D RuleID, the eight-byte VXLAN header, and the frame itself (Binding
/// and Profile section).
pub fn make_packet(vxlan_hdr: &[u8; 8], frame: &[u8]) -> Vec<u8> {
    let mut packet = Vec::with_capacity(1 + 8 + frame.len());
    packet.push(CD_RULE);
    packet.extend_from_slice(vxlan_hdr);
    packet.extend_from_slice(frame);
    packet
}

/// Send one Ethernet frame on an association configured for this profile:
/// one UDP payload if the SCHC Packet fits the PMTU unfragmented, otherwise
/// K fragment UDP payloads in sender order.
///
/// `tag` is the DTag allocated for this packet (one per packet, reused on
/// every fragment of it); callers own DTag allocation and uniqueness
/// (DTag Reuse and Restart section).
pub fn send(
    vxlan_hdr: &[u8; 8],
    frame: &[u8],
    tag: u64,
    cfg: &SendConfig,
) -> Result<Vec<Vec<u8>>, WireError> {
    let packet = make_packet(vxlan_hdr, frame);

    if packet.len() <= cfg.pmtu.saturating_sub(cfg.h_ip).saturating_sub(8) {
        return Ok(vec![packet]);
    }

    let a = cfg.variant.all1_header_len();
    let budget = cfg.pmtu.saturating_sub(cfg.h_ip).saturating_sub(8);
    if budget <= a {
        return Err(WireError::FieldOverflow);
    }
    let c = budget - a;
    // The draft requires C >= 9 so every fragment can carry the first
    // marker's nine-byte envelope (C/D RuleID + VXLAN header).
    if c < 9 {
        return Err(WireError::FieldOverflow);
    }
    let k = (packet.len() + c - 1) / c; // ceil(len / c)
    if !(2..=8).contains(&k) {
        return Err(WireError::FieldOverflow);
    }

    let fcns = fcn_sequence(k);
    let mut out = Vec::with_capacity(k);
    for (i, &f) in fcns.iter().enumerate() {
        let start = i * c;
        let end = ((i + 1) * c).min(packet.len());
        let slice = &packet[start..end];
        let rcs = if f == 15 { Some(crc32c(&packet)) } else { None };
        out.push(encode(cfg.variant, cfg.fr_rule, tag, f, slice, rcs)?);
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wire::classify;

    #[test]
    fn unfragmented_boundary_ipv4_pmtu_1280() {
        let cfg = SendConfig { variant: Variant::Compact, pmtu: 1280, h_ip: 20, fr_rule: 1 };
        let hdr = crate::wire::VXLAN_HDR_EXAMPLE;

        let at = send(&hdr, &vec![0u8; 1243], 1, &cfg).unwrap();
        assert_eq!(at.len(), 1);
        assert!(matches!(classify(cfg.variant, &at[0]).unwrap(), crate::wire::Classification::Packet(_)));
        assert_eq!(20 + 8 + at[0].len(), 1280);

        let over = send(&hdr, &vec![0u8; 1244], 1, &cfg).unwrap();
        assert_eq!(over.len(), 2);
        for m in &over {
            assert!(matches!(classify(cfg.variant, m).unwrap(), crate::wire::Classification::Fragment(_)));
            assert!(20 + 8 + m.len() <= 1280);
        }
    }

    #[test]
    fn worked_example_1514_byte_frame() {
        let cfg = SendConfig { variant: Variant::Compact, pmtu: 1280, h_ip: 20, fr_rule: 1 };
        let hdr = crate::wire::VXLAN_HDR_EXAMPLE;
        let lab = send(&hdr, &vec![0u8; 1514], 1, &cfg).unwrap();
        assert_eq!(lab.len(), 2);
        assert_eq!(lab[0].len() - 5, 1243);
        assert_eq!(lab[1].len() - 9, 280);
    }
}
