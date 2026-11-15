//! Conformance gate required by `interop/README.md` section 4: the encoder
//! must reproduce every `udp_payload_hex` in `draft/revised/wire-vectors.json`
//! byte for byte, and the decoder must accept those vectors and reject the
//! malformed cases that `draft/revised/check_vectors.py` rejects.
//!
//! `reassemble_any_order` below is a test-only, permutation-tolerant
//! reassembly checker ported from `check_vectors.py`'s `reassemble()`. It is
//! not either of the draft's two receivers (Ordered Receiver / Optional
//! Reordering-Tolerant Receiver) -- it exists only to confirm, independently
//! of arrival order, that the wire codec round-trips.

use schcvx::crc32c::crc32c;
use schcvx::sender::{make_packet, send, SendConfig};
use schcvx::wire::{classify, decode, encode, Classification, Variant, VXLAN_HDR_EXAMPLE};

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord)]
enum SlotKey {
    Regular(u8),
    Final,
}

fn reassemble_any_order(variant: Variant, messages: &[Vec<u8>]) -> Result<Option<Vec<u8>>, &'static str> {
    use std::collections::BTreeMap;

    let mut slots: BTreeMap<SlotKey, (u8, Vec<u8>, Option<u32>)> = BTreeMap::new();
    let mut identity: Option<(u64, u64)> = None;

    for m in messages {
        let d = decode(variant, m).map_err(|_| "decode error")?;
        if d.abort {
            return Err("abort");
        }
        let id = (d.rule, d.tag);
        if let Some(prev) = identity {
            if prev != id {
                return Err("mixed identity");
            }
        }
        identity = Some(id);
        let key = if d.fcn == 15 { SlotKey::Final } else { SlotKey::Regular(d.fcn & 7) };
        let value = (d.fcn, d.payload.to_vec(), d.rcs);
        match slots.get(&key) {
            Some(existing) if existing != &value => return Err("conflicting duplicate"),
            Some(_) => {}
            None => {
                slots.insert(key, value);
            }
        }
    }

    let firsts: Vec<_> = slots.values().filter(|(fcn, _, _)| (8..=14).contains(fcn)).collect();
    if firsts.len() != 1 {
        return Ok(None);
    }
    let n = firsts[0].0 & 7;

    let mut expected: std::collections::BTreeSet<SlotKey> = (0..=n).map(SlotKey::Regular).collect();
    expected.insert(SlotKey::Final);
    let present: std::collections::BTreeSet<SlotKey> = slots.keys().cloned().collect();
    if !present.is_subset(&expected) {
        return Err("unexpected position");
    }
    if present != expected {
        return Ok(None);
    }

    let mut tiles: Vec<Vec<u8>> = Vec::new();
    for i in (0..=n).rev() {
        tiles.push(slots[&SlotKey::Regular(i)].1.clone());
    }
    let final_slot = slots.get(&SlotKey::Final).unwrap();
    let lens: std::collections::BTreeSet<usize> = tiles.iter().map(|t| t.len()).collect();
    if lens.len() != 1 || final_slot.1.len() > tiles[0].len() {
        return Err("tile lengths");
    }

    let mut packet = Vec::new();
    for t in &tiles {
        packet.extend_from_slice(t);
    }
    packet.extend_from_slice(&final_slot.1);
    if crc32c(&packet) != final_slot.2.unwrap() {
        return Err("RCS");
    }
    Ok(Some(packet))
}

/// Heap's algorithm; good enough for the K<=8 message lists used here.
fn permutations<T: Clone>(items: &[T]) -> Vec<Vec<T>> {
    let n = items.len();
    let mut v = items.to_vec();
    let mut out = Vec::new();
    if n == 0 {
        out.push(v);
        return out;
    }
    fn permute<T: Clone>(items: &mut Vec<T>, k: usize, out: &mut Vec<Vec<T>>) {
        if k == 1 {
            out.push(items.clone());
            return;
        }
        for i in 0..k {
            permute(items, k - 1, out);
            if k % 2 == 0 {
                items.swap(i, k - 1);
            } else {
                items.swap(0, k - 1);
            }
        }
    }
    permute(&mut v, n, &mut out);
    out
}

const VARIANTS: [Variant; 2] = [Variant::Compact, Variant::Extended];

#[test]
fn fragment_roundtrip_all_orders_and_k() {
    for &variant in &VARIANTS {
        for k in 2..=8usize {
            let mut packet = vec![0u8];
            packet.extend((1..k * 9).map(|v| v as u8));
            let fcns = schcvx::wire::fcn_sequence(k);
            let messages: Vec<Vec<u8>> = fcns
                .iter()
                .enumerate()
                .map(|(i, &f)| {
                    let tile = &packet[i * 9..(i + 1) * 9];
                    let rcs = if f == 15 { Some(crc32c(&packet)) } else { None };
                    encode(variant, 1, 1, f, tile, rcs).unwrap()
                })
                .collect();

            for order in permutations(&messages) {
                assert_eq!(reassemble_any_order(variant, &order).unwrap(), Some(packet.clone()));
            }
            for i in 0..k {
                let mut missing = messages.clone();
                missing.remove(i);
                assert_eq!(reassemble_any_order(variant, &missing).unwrap(), None);
            }
            let mut with_dup = messages.clone();
            with_dup.push(messages[0].clone());
            assert_eq!(reassemble_any_order(variant, &with_dup).unwrap(), Some(packet.clone()));

            let mut bad = messages.last().unwrap().clone();
            let last = bad.len() - 1;
            bad[last] ^= 1;
            let mut with_bad = messages.clone();
            with_bad.push(bad.clone());
            assert!(reassemble_any_order(variant, &with_bad).is_err());
            let mut replace_last = messages[..messages.len() - 1].to_vec();
            replace_last.push(bad);
            assert!(reassemble_any_order(variant, &replace_last).is_err());
        }

        let h = variant.header_len();
        for length in 0..h {
            assert!(decode(variant, &vec![0u8; length]).is_err());
        }
        for extra in 1..5 {
            let msg = encode(variant, 1, 1, 15, &vec![0u8; extra], None).unwrap();
            assert!(decode(variant, &msg).is_err());
        }
        for f in [6u8, 7u8] {
            let msg = encode(variant, 1, 1, f, b"x", None).unwrap();
            assert!(decode(variant, &msg).is_err());
        }
    }
}

#[test]
fn unfragmented_packet_boundaries() {
    for &variant in &VARIANTS {
        for &(p, h, fit) in &[(1500usize, 20usize, 1463usize), (1500, 40, 1443), (1280, 20, 1243)] {
            let cfg = SendConfig { variant, pmtu: p, h_ip: h, fr_rule: 1 };
            let at = send(&VXLAN_HDR_EXAMPLE, &vec![0u8; fit], 1, &cfg).unwrap();
            assert_eq!(at.len(), 1);
            assert!(matches!(classify(variant, &at[0]).unwrap(), Classification::Packet(_)));
            assert_eq!(h + 8 + at[0].len(), p);

            let over = send(&VXLAN_HDR_EXAMPLE, &vec![0u8; fit + 1], 1, &cfg).unwrap();
            assert_eq!(over.len(), 2);
            for m in &over {
                assert!(matches!(classify(variant, m).unwrap(), Classification::Fragment(_)));
                assert!(h + 8 + m.len() <= p);
            }
            let expected = make_packet(&VXLAN_HDR_EXAMPLE, &vec![0u8; fit + 1]);
            assert_eq!(reassemble_any_order(variant, &over).unwrap(), Some(expected));
        }

        let short = make_packet(&VXLAN_HDR_EXAMPLE, &vec![0u8; 13]);
        assert!(classify(variant, &short).is_err());

        let unknown_rule = encode(variant, 2, 1, 8, &vec![0u8; 9], None).unwrap();
        assert!(classify(variant, &unknown_rule).is_err());
    }
}

#[test]
fn lab_boundary_1280_pmtu_1514_byte_frame() {
    let cfg = SendConfig { variant: Variant::Compact, pmtu: 1280, h_ip: 20, fr_rule: 1 };
    let lab = send(&VXLAN_HDR_EXAMPLE, &vec![0u8; 1514], 1, &cfg).unwrap();
    assert_eq!(lab[0].len() - 5, 1243);
    assert_eq!(lab[1].len() - 9, 280);
}

#[test]
fn wire_vectors_json_byte_for_byte() {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../../draft/revised/wire-vectors.json");
    let text = std::fs::read_to_string(path).expect("read wire-vectors.json");
    let vectors: serde_json::Value = serde_json::from_str(&text).expect("parse wire-vectors.json");

    // 'unfragmented': NoCompress C/D RuleID, VXLAN header, 64-byte frame; P=1500, H=20.
    let frame64: Vec<u8> = (0..64u32).map(|i| (i % 256) as u8).collect();
    let cfg = SendConfig { variant: Variant::Compact, pmtu: 1500, h_ip: 20, fr_rule: 1 };
    let unfragmented = send(&VXLAN_HDR_EXAMPLE, &frame64, 1, &cfg).unwrap();
    assert_eq!(unfragmented.len(), 1);
    assert_eq!(hex(&unfragmented[0]), vectors["unfragmented"]["udp_payload_hex"].as_str().unwrap());

    // The 1514-byte frame, byte[i] = i % 256, split at C for each variant.
    let frame1514: Vec<u8> = (0..1514u32).map(|i| (i % 256) as u8).collect();
    let packet = make_packet(&VXLAN_HDR_EXAMPLE, &frame1514);
    let rcs = crc32c(&packet);

    for (variant, key) in [(Variant::Compact, "32"), (Variant::Extended, "60")] {
        let h = variant.header_len();
        let c = 1500 - 20 - 8 - h - 4;
        let parts = [&packet[..c], &packet[c..]];
        let messages = vec![
            encode(variant, 1, 1, 8, parts[0], None).unwrap(),
            encode(variant, 1, 1, 15, parts[1], Some(rcs)).unwrap(),
        ];
        assert_eq!(reassemble_any_order(variant, &messages).unwrap(), Some(packet.clone()));

        let v = &vectors[key];
        assert_eq!(v["packet_bytes"].as_u64().unwrap() as usize, packet.len());
        assert_eq!(v["rcs"].as_str().unwrap(), format!("{rcs:08x}"));
        assert_eq!(
            v["tile_lengths"].as_array().unwrap().iter().map(|x| x.as_u64().unwrap() as usize).collect::<Vec<_>>(),
            vec![parts[0].len(), parts[1].len()]
        );
        assert_eq!(
            v["ip_lengths"].as_array().unwrap().iter().map(|x| x.as_u64().unwrap() as usize).collect::<Vec<_>>(),
            vec![28 + messages[0].len(), 28 + messages[1].len()]
        );
        let expected_hex: Vec<&str> = v["udp_payload_hex"].as_array().unwrap().iter().map(|x| x.as_str().unwrap()).collect();
        assert_eq!(hex(&messages[0]), expected_hex[0]);
        assert_eq!(hex(&messages[1]), expected_hex[1]);
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
