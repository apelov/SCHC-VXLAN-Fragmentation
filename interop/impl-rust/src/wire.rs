//! Wire codec for the SCHC No-ACK VXLAN fragmentation profile: the RuleID /
//! DTag / FCN fragment header (Fragment Format section of the revised
//! draft) plus classification of unfragmented SCHC Packets vs. fragments
//! (Binding and Profile section). Mirrors `draft/revised/check_vectors.py`
//! field-for-field; see `tests/vectors.rs` for the byte-for-byte conformance
//! checks against `draft/revised/wire-vectors.json`.

/// NoCompress C/D RuleID: identifies an unfragmented SCHC Packet.
pub const CD_RULE: u8 = 0;

/// The fragmentation RuleID used throughout the lab profile and vectors.
pub const FR_RULE: u64 = 1;

/// The eight-byte VXLAN header used in the wire vectors: I flag set, VNI
/// 12345 (0x003039).
pub const VXLAN_HDR_EXAMPLE: [u8; 8] = [0x08, 0x00, 0x00, 0x00, 0x00, 0x30, 0x39, 0x00];

/// A configured wire variant (Fragment Format section, "Configured wire
/// variants" table).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Variant {
    /// RuleID 4 bits, DTag 32 bits, FCN 4 bits: 5-byte regular header, 9-byte All-1 header.
    Compact,
    /// RuleID 8 bits, DTag 60 bits, FCN 4 bits: 9-byte regular header, 13-byte All-1 header.
    Extended,
}

impl Variant {
    pub const fn rule_bits(self) -> u32 {
        match self {
            Variant::Compact => 4,
            Variant::Extended => 8,
        }
    }

    pub const fn dtag_bits(self) -> u32 {
        match self {
            Variant::Compact => 32,
            Variant::Extended => 60,
        }
    }

    /// Length of the regular fragment header, in bytes.
    pub const fn header_len(self) -> usize {
        match self {
            Variant::Compact => 5,
            Variant::Extended => 9,
        }
    }

    /// Length of the All-1 header (regular header + 4-byte RCS), in bytes.
    pub const fn all1_header_len(self) -> usize {
        self.header_len() + 4
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WireError {
    /// RuleID, DTag or FCN does not fit in its configured field width.
    FieldOverflow,
    /// Fewer bytes than the regular header length.
    ShortHeader,
    /// FCN is 6 or 7 (invalid), or the datagram is shorter than the header
    /// plus the minimum payload/trailer for its FCN.
    Malformed,
    /// A first-marked regular fragment (FCN 8..14) has fewer than 9 payload
    /// bytes or does not start with the NoCompress C/D RuleID.
    FirstFragmentEnvelope,
    /// The fragment's RuleID does not match the association's configured
    /// fragmentation RuleID.
    UnknownRule,
    /// An unfragmented SCHC Packet shorter than 1 (C/D RuleID) + 8 (VXLAN
    /// header) + 14 (minimum Ethernet header) bytes.
    ShortUnfragmentedPacket,
}

/// A decoded fragment. `abort` is set for a header-only All-1 datagram
/// (Sender-Abort, RFC 8724 Section 8.3.4): it carries no data and MUST NOT
/// be interpreted as a payload-bearing fragment.
#[derive(Debug, Clone, Copy)]
pub struct Decoded<'a> {
    pub rule: u64,
    pub tag: u64,
    pub fcn: u8,
    pub payload: &'a [u8],
    pub rcs: Option<u32>,
    pub abort: bool,
}

/// Pack one fragment header (RuleID | DTag | FCN), followed by the RCS (if
/// given, i.e. for the All-1 fragment) and the payload.
pub fn encode(
    variant: Variant,
    rule: u64,
    tag: u64,
    fcn: u8,
    payload: &[u8],
    rcs: Option<u32>,
) -> Result<Vec<u8>, WireError> {
    let rb = variant.rule_bits();
    let bits = variant.dtag_bits();
    if rule >= (1u64 << rb) || (bits < 64 && tag >= (1u64 << bits)) || fcn >= 16 {
        return Err(WireError::FieldOverflow);
    }
    let word: u128 = ((rule as u128) << (bits + 4)) | ((tag as u128) << 4) | (fcn as u128);
    let nbytes = variant.header_len();
    let mut out = Vec::with_capacity(nbytes + 4 + payload.len());
    for i in (0..nbytes).rev() {
        out.push(((word >> (8 * i)) & 0xff) as u8);
    }
    if let Some(r) = rcs {
        out.extend_from_slice(&r.to_be_bytes());
    }
    out.extend_from_slice(payload);
    Ok(out)
}

/// Parse one fragment header and locate its payload (and RCS, for All-1).
pub fn decode(variant: Variant, data: &[u8]) -> Result<Decoded<'_>, WireError> {
    let h = variant.header_len();
    if data.len() < h {
        return Err(WireError::ShortHeader);
    }
    let bits = variant.dtag_bits();
    let mut word: u128 = 0;
    for &b in &data[..h] {
        word = (word << 8) | b as u128;
    }
    let fcn = (word & 0xf) as u8;
    let tag = ((word >> 4) & ((1u128 << bits) - 1)) as u64;
    let rule = (word >> (bits + 4)) as u64;

    if fcn == 15 && data.len() == h {
        return Ok(Decoded { rule, tag, fcn, payload: &[], rcs: None, abort: true });
    }
    let min_len = h + if fcn == 15 { 5 } else { 1 };
    if fcn == 6 || fcn == 7 || data.len() < min_len {
        return Err(WireError::Malformed);
    }
    let payload: &[u8] = if fcn == 15 { &data[h + 4..] } else { &data[h..] };
    if (8..=14).contains(&fcn) && (payload.len() < 9 || payload[0] != CD_RULE) {
        return Err(WireError::FirstFragmentEnvelope);
    }
    let rcs = if fcn == 15 {
        Some(u32::from_be_bytes(data[h..h + 4].try_into().unwrap()))
    } else {
        None
    };
    Ok(Decoded { rule, tag, fcn, payload, rcs, abort: false })
}

/// What one UDP payload is, once the peer association (and so the wire
/// variant) is already selected: an unfragmented SCHC Packet or a fragment.
#[derive(Debug, Clone, Copy)]
pub enum Classification<'a> {
    Packet(&'a [u8]),
    Fragment(&'a [u8]),
}

/// Classify a UDP payload on an already-selected association (Binding and
/// Profile section): a leading NoCompress C/D RuleID byte means an
/// unfragmented SCHC Packet; anything else is parsed as a fragment using the
/// association's configured fragmentation RuleID.
pub fn classify(variant: Variant, data: &[u8]) -> Result<Classification<'_>, WireError> {
    if !data.is_empty() && data[0] == CD_RULE {
        if data.len() < 1 + 8 + 14 {
            return Err(WireError::ShortUnfragmentedPacket);
        }
        return Ok(Classification::Packet(data));
    }
    let d = decode(variant, data)?;
    if d.rule != FR_RULE {
        return Err(WireError::UnknownRule);
    }
    Ok(Classification::Fragment(data))
}

/// The sender-order FCN sequence for a K-fragment packet (FCN Values
/// section): `1000,1111` for K=2 up to `1110,0101,...,0000,1111` for K=8.
pub fn fcn_sequence(k: usize) -> Vec<u8> {
    assert!((2..=8).contains(&k), "K must be 2..=8, got {k}");
    let mut v = Vec::with_capacity(k);
    v.push((8 + k - 2) as u8);
    if k >= 3 {
        for c in (0..=(k as i64 - 3)).rev() {
            v.push(c as u8);
        }
    }
    v.push(15);
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fixed_bit_pattern_anchors() {
        // Independent anchors from check_vectors.py, one per variant.
        assert_eq!(hex(&encode(Variant::Compact, 1, 1, 8, &[], None).unwrap()), "1000000018");
        assert_eq!(
            hex(&encode(Variant::Extended, 1, 1, 8, &[], None).unwrap()),
            "010000000000000018"
        );
    }

    #[test]
    fn fcn_sequences_match_table() {
        assert_eq!(fcn_sequence(2), vec![0b1000, 0b1111]);
        assert_eq!(fcn_sequence(3), vec![0b1001, 0b0000, 0b1111]);
        assert_eq!(fcn_sequence(4), vec![0b1010, 0b0001, 0b0000, 0b1111]);
        assert_eq!(
            fcn_sequence(8),
            vec![0b1110, 0b0101, 0b0100, 0b0011, 0b0010, 0b0001, 0b0000, 0b1111]
        );
    }

    fn hex(bytes: &[u8]) -> String {
        bytes.iter().map(|b| format!("{b:02x}")).collect()
    }
}
