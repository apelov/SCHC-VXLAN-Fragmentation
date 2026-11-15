//! The two receivers described in the draft: the required-by-default
//! Ordered Receiver (RFC 8724 Section 8.4.1 No-ACK, unmodified) and the
//! Optional Reordering-Tolerant Receiver. Both operate purely on decoded
//! fragments and a caller-supplied clock, so they need no socket I/O and
//! are fully unit-testable; `bin/vtep.rs` is what wires a transport to
//! them.
//!
//! Scope note: each receiver instance here covers one peer association
//! (the caller is expected to hold one instance per configured
//! association). Entries are additionally keyed by (RuleID, DTag) inside
//! that scope, per the draft.

use std::collections::HashMap;
use std::time::{Duration, Instant};

use crate::crc32c::crc32c;
use crate::wire::{decode, Variant, FR_RULE};

type Key = (u64, u64); // (rule, tag)

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DropReason {
    Malformed,
    UnknownRule,
    Aborted,
    IntegrityFailure,
    Expired,
    Late,
    TableExhaustion,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Outcome {
    /// A complete, RCS-verified packet (NoCompress C/D RuleID + VXLAN header + frame).
    Delivered(Vec<u8>),
    /// The fragment was admitted but its packet is not complete yet.
    Pending,
    Dropped(DropReason),
}

/// Counters maintained per peer association (Security/Manageability
/// Considerations sections). `peer_admission_failure` is incremented by the
/// caller, at peer/VNI selection, before a fragment ever reaches here.
#[derive(Debug, Clone, Copy, Default)]
pub struct Counters {
    pub completed: u64,
    pub expired: u64,
    pub late: u64,
    pub duplicate: u64,
    pub integrity_failure: u64,
    pub malformed: u64,
    pub peer_admission_failure: u64,
    pub table_exhaustion: u64,
}

// --- Ordered Receiver -------------------------------------------------

struct OrderedEntry {
    buf: Vec<u8>,
    inactivity_deadline: Instant,
}

/// RFC 8724 Section 8.4.1 No-ACK, unmodified: fragments are assembled in
/// *reception* order (not by FCN position), the inactivity timer resets on
/// every regular fragment, and an All-1 fragment concludes reassembly
/// immediately -- delivering on a good RCS, dropping on a bad one -- even
/// if fragments are still missing. This is the lab profile's default
/// receiver and requires an in-order path.
pub struct OrderedReceiver {
    variant: Variant,
    inactivity_timeout: Duration,
    max_entries: usize,
    entries: HashMap<Key, OrderedEntry>,
    pub counters: Counters,
}

impl OrderedReceiver {
    pub fn new(variant: Variant, inactivity_timeout: Duration, max_entries: usize) -> Self {
        Self { variant, inactivity_timeout, max_entries, entries: HashMap::new(), counters: Counters::default() }
    }

    pub fn on_fragment(&mut self, data: &[u8], now: Instant) -> Outcome {
        let d = match decode(self.variant, data) {
            Ok(d) => d,
            Err(_) => {
                self.counters.malformed += 1;
                return Outcome::Dropped(DropReason::Malformed);
            }
        };
        if d.rule != FR_RULE {
            self.counters.malformed += 1;
            return Outcome::Dropped(DropReason::UnknownRule);
        }
        let key = (d.rule, d.tag);
        if d.abort {
            self.entries.remove(&key);
            return Outcome::Dropped(DropReason::Aborted);
        }

        if d.fcn == 15 {
            // Concludes reassembly unconditionally, complete or not.
            let entry = self.entries.remove(&key);
            let mut buf = entry.map(|e| e.buf).unwrap_or_default();
            buf.extend_from_slice(d.payload);
            if d.rcs == Some(crc32c(&buf)) {
                self.counters.completed += 1;
                Outcome::Delivered(buf)
            } else {
                self.counters.integrity_failure += 1;
                Outcome::Dropped(DropReason::IntegrityFailure)
            }
        } else {
            if !self.entries.contains_key(&key) && self.entries.len() >= self.max_entries {
                self.counters.table_exhaustion += 1;
                return Outcome::Dropped(DropReason::TableExhaustion);
            }
            let entry = self.entries.entry(key).or_insert_with(|| OrderedEntry {
                buf: Vec::new(),
                inactivity_deadline: now + self.inactivity_timeout,
            });
            entry.buf.extend_from_slice(d.payload);
            entry.inactivity_deadline = now + self.inactivity_timeout;
            Outcome::Pending
        }
    }

    /// Drop entries whose inactivity timer has expired; returns how many.
    pub fn sweep(&mut self, now: Instant) -> usize {
        let before = self.entries.len();
        self.entries.retain(|_, e| e.inactivity_deadline > now);
        let dropped = before - self.entries.len();
        self.counters.expired += dropped as u64;
        dropped
    }
}

// --- Optional Reordering-Tolerant Receiver -----------------------------

struct TolerantEntry {
    /// position (0..=n) -> (is_first, payload)
    regular: HashMap<u8, (bool, Vec<u8>)>,
    final_: Option<(Vec<u8>, u32)>,
    n: Option<u8>,
    deadline: Instant,
    inactivity_deadline: Instant,
}

/// The experimental receiver of the "Optional Reordering-Tolerant
/// Receiver" section: holds state keyed by (RuleID, DTag), learns the
/// expected fragment count from whichever fragment carries the first
/// marker, and completes once every expected position plus the final
/// fragment have arrived -- in any order. A fixed total deadline (`Delta`)
/// bounds each entry; an optional inactivity timer, reset on every regular
/// arrival, can expire sooner.
pub struct TolerantReceiver {
    variant: Variant,
    deadline: Duration,
    inactivity_timeout: Option<Duration>,
    max_entries: usize,
    retirement: Duration,
    entries: HashMap<Key, TolerantEntry>,
    retired: HashMap<Key, Instant>, // retired-until
    pub counters: Counters,
}

impl TolerantReceiver {
    pub fn new(
        variant: Variant,
        deadline: Duration,
        inactivity_timeout: Option<Duration>,
        retirement: Duration,
        max_entries: usize,
    ) -> Self {
        Self {
            variant,
            deadline,
            inactivity_timeout,
            max_entries,
            retirement,
            entries: HashMap::new(),
            retired: HashMap::new(),
            counters: Counters::default(),
        }
    }

    pub fn on_fragment(&mut self, data: &[u8], now: Instant) -> Outcome {
        let d = match decode(self.variant, data) {
            Ok(d) => d,
            Err(_) => {
                self.counters.malformed += 1;
                return Outcome::Dropped(DropReason::Malformed);
            }
        };
        if d.rule != FR_RULE {
            self.counters.malformed += 1;
            return Outcome::Dropped(DropReason::UnknownRule);
        }
        let key = (d.rule, d.tag);
        if d.abort {
            self.entries.remove(&key);
            self.retired.insert(key, now + self.retirement);
            return Outcome::Dropped(DropReason::Aborted);
        }
        if let Some(&until) = self.retired.get(&key) {
            if now < until {
                self.counters.late += 1;
                return Outcome::Dropped(DropReason::Late);
            }
        }

        if !self.entries.contains_key(&key) {
            if self.entries.len() >= self.max_entries {
                self.counters.table_exhaustion += 1;
                return Outcome::Dropped(DropReason::TableExhaustion);
            }
            self.entries.insert(
                key,
                TolerantEntry {
                    regular: HashMap::new(),
                    final_: None,
                    n: None,
                    deadline: now + self.deadline,
                    inactivity_deadline: now + self.inactivity_timeout.unwrap_or(self.deadline),
                },
            );
        }

        let conflict = {
            let entry = self.entries.get_mut(&key).unwrap();
            if d.fcn == 15 {
                let candidate = (d.payload.to_vec(), d.rcs.unwrap());
                match &entry.final_ {
                    Some(existing) if existing != &candidate => true,
                    Some(_) => {
                        self.counters.duplicate += 1;
                        false
                    }
                    None => {
                        entry.final_ = Some(candidate);
                        false
                    }
                }
            } else {
                let pos = d.fcn & 7;
                let is_first = (8..=14).contains(&d.fcn);

                // A second first marker with a different countdown is a conflict.
                let n_conflict = is_first
                    && match entry.n {
                        Some(n) => n != pos,
                        None => {
                            entry.n = Some(pos);
                            false
                        }
                    };

                // A first marker colliding with an already-stored non-first
                // position, or any position's payload changing, is also a
                // conflict; an identical duplicate is just counted.
                let candidate = (is_first, d.payload.to_vec());
                let pos_conflict = match entry.regular.get(&pos) {
                    Some(existing) if existing != &candidate => true,
                    Some(_) => {
                        self.counters.duplicate += 1;
                        false
                    }
                    None => {
                        entry.regular.insert(pos, candidate);
                        false
                    }
                };
                n_conflict || pos_conflict
            }
        };
        if conflict {
            self.entries.remove(&key);
            self.retired.insert(key, now + self.retirement);
            self.counters.integrity_failure += 1;
            return Outcome::Dropped(DropReason::IntegrityFailure);
        }

        // Positions outside the now-known range are themselves a conflict.
        if let Some(entry) = self.entries.get(&key) {
            if let Some(n) = entry.n {
                if entry.regular.keys().any(|&p| p > n) {
                    self.entries.remove(&key);
                    self.retired.insert(key, now + self.retirement);
                    self.counters.integrity_failure += 1;
                    return Outcome::Dropped(DropReason::IntegrityFailure);
                }
            }
        }

        if d.fcn != 15 {
            if let (Some(entry), Some(timeout)) = (self.entries.get_mut(&key), self.inactivity_timeout) {
                entry.inactivity_deadline = now + timeout;
            }
        }

        let complete = {
            let entry = self.entries.get(&key).unwrap();
            match entry.n {
                Some(n) => entry.final_.is_some() && (0..=n).all(|p| entry.regular.contains_key(&p)),
                None => false,
            }
        };
        if !complete {
            return Outcome::Pending;
        }

        let entry = self.entries.remove(&key).unwrap();
        self.retired.insert(key, now + self.retirement);
        let n = entry.n.unwrap();
        let mut packet = Vec::new();
        for p in (0..=n).rev() {
            packet.extend_from_slice(&entry.regular[&p].1);
        }
        let (final_payload, rcs) = entry.final_.unwrap();
        packet.extend_from_slice(&final_payload);
        if crc32c(&packet) == rcs {
            self.counters.completed += 1;
            Outcome::Delivered(packet)
        } else {
            self.counters.integrity_failure += 1;
            Outcome::Dropped(DropReason::IntegrityFailure)
        }
    }

    /// Drop entries past their deadline/inactivity timeout -- retiring
    /// their identifier, same as a completion or abort would -- and expire
    /// retirement records; returns (entries_expired, retirements_expired).
    pub fn sweep(&mut self, now: Instant) -> (usize, usize) {
        let expired_keys: Vec<Key> = self
            .entries
            .iter()
            .filter(|(_, e)| e.deadline <= now || e.inactivity_deadline <= now)
            .map(|(k, _)| *k)
            .collect();
        for key in &expired_keys {
            self.entries.remove(key);
            self.retired.insert(*key, now + self.retirement);
        }
        self.counters.expired += expired_keys.len() as u64;

        let before_r = self.retired.len();
        self.retired.retain(|_, &mut until| until > now);
        let retirements_expired = before_r - self.retired.len();
        (expired_keys.len(), retirements_expired)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sender::{send, SendConfig};
    use crate::wire::VXLAN_HDR_EXAMPLE;

    fn fragments(frame_len: usize) -> Vec<Vec<u8>> {
        let cfg = SendConfig { variant: Variant::Compact, pmtu: 1280, h_ip: 20, fr_rule: 1 };
        send(&VXLAN_HDR_EXAMPLE, &vec![0xab; frame_len], 7, &cfg).unwrap()
    }

    #[test]
    fn ordered_receiver_in_order_delivers() {
        let mut r = OrderedReceiver::new(Variant::Compact, Duration::from_millis(10), 16);
        let now = Instant::now();
        let frags = fragments(1514);
        assert_eq!(r.on_fragment(&frags[0], now), Outcome::Pending);
        match r.on_fragment(&frags[1], now) {
            Outcome::Delivered(_) => {}
            other => panic!("expected delivery, got {other:?}"),
        }
        assert_eq!(r.counters.completed, 1);
    }

    #[test]
    fn ordered_receiver_early_all1_concludes_incomplete() {
        let mut r = OrderedReceiver::new(Variant::Compact, Duration::from_millis(10), 16);
        let now = Instant::now();
        let frags = fragments(1514);
        // All-1 arrives first: RCS is over the full packet, but only the
        // All-1 payload has been appended, so the check must fail.
        assert_eq!(r.on_fragment(&frags[1], now), Outcome::Dropped(DropReason::IntegrityFailure));
        assert_eq!(r.counters.integrity_failure, 1);
    }

    #[test]
    fn ordered_receiver_inactivity_sweep() {
        let mut r = OrderedReceiver::new(Variant::Compact, Duration::from_millis(10), 16);
        let now = Instant::now();
        let frags = fragments(1514);
        r.on_fragment(&frags[0], now);
        assert_eq!(r.sweep(now + Duration::from_millis(11)), 1);
        assert_eq!(r.counters.expired, 1);
    }

    #[test]
    fn tolerant_receiver_last_fragment_first() {
        let mut r = TolerantReceiver::new(
            Variant::Compact,
            Duration::from_millis(10),
            None,
            Duration::from_millis(100),
            16,
        );
        let now = Instant::now();
        let frags = fragments(1514);
        assert_eq!(r.on_fragment(&frags[1], now), Outcome::Pending);
        match r.on_fragment(&frags[0], now + Duration::from_millis(1)) {
            Outcome::Delivered(_) => {}
            other => panic!("expected delivery, got {other:?}"),
        }
        assert_eq!(r.counters.completed, 1);
    }

    #[test]
    fn tolerant_receiver_deadline_expiry_then_late() {
        let mut r = TolerantReceiver::new(
            Variant::Compact,
            Duration::from_millis(5),
            None,
            Duration::from_millis(50),
            16,
        );
        let now = Instant::now();
        let frags = fragments(1514);
        r.on_fragment(&frags[1], now);
        r.sweep(now + Duration::from_millis(6));
        assert_eq!(r.counters.expired, 1);
        assert_eq!(r.on_fragment(&frags[0], now + Duration::from_millis(7)), Outcome::Dropped(DropReason::Late));
        assert_eq!(r.counters.late, 1);
    }

    #[test]
    fn tolerant_receiver_conflicting_final_aborts() {
        let mut r = TolerantReceiver::new(
            Variant::Compact,
            Duration::from_millis(10),
            None,
            Duration::from_millis(50),
            16,
        );
        let now = Instant::now();
        let frags = fragments(1514);
        r.on_fragment(&frags[1], now);
        let mut tampered = frags[1].clone();
        *tampered.last_mut().unwrap() ^= 1;
        assert_eq!(r.on_fragment(&tampered, now), Outcome::Dropped(DropReason::IntegrityFailure));
    }
}
