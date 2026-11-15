//! Forwarding filter for the LAN-side capture loop: a firewall-style
//! allow-list that decides which captured frames are sent over the
//! configured peer association. A frame matching no rule is left alone
//! (not tunneled) -- the default is deny, not fall-through-to-VXLAN,
//! since this host owns no other path for it.
//!
//! VXLAN is an L2 overlay: a host on one side's LAN can only resolve a
//! host on the other side's LAN if ARP itself crosses the tunnel, so an
//! ARP request/reply mentioning an address in a configured subnet is
//! forwarded exactly like a matching IPv4 unicast packet is. Only IPv4
//! (and ARP-for-IPv4) matching is implemented; other L2 broadcast/
//! multicast traffic (IPv6 NDP, DHCP, mDNS, ...) never matches today --
//! extend `Filter::allows` if that's needed too.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Ipv4Net {
    pub addr: [u8; 4],
    pub prefix_len: u8,
}

impl Ipv4Net {
    /// Parse `"a.b.c.d/n"`.
    pub fn parse(s: &str) -> Option<Self> {
        let (a, p) = s.split_once('/')?;
        let prefix_len: u8 = p.parse().ok()?;
        if prefix_len > 32 {
            return None;
        }
        let mut addr = [0u8; 4];
        let mut parts = a.split('.');
        for byte in addr.iter_mut() {
            *byte = parts.next()?.parse().ok()?;
        }
        if parts.next().is_some() {
            return None;
        }
        Some(Ipv4Net { addr, prefix_len })
    }

    fn mask(self) -> u32 {
        if self.prefix_len == 0 {
            0
        } else {
            u32::MAX << (32 - self.prefix_len as u32)
        }
    }

    pub fn contains(self, ip: [u8; 4]) -> bool {
        let mask = self.mask();
        (u32::from_be_bytes(self.addr) & mask) == (u32::from_be_bytes(ip) & mask)
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Rule {
    pub dst: Ipv4Net,
}

#[derive(Clone, Debug, Default)]
pub struct Filter {
    pub rules: Vec<Rule>,
}

const ETHERTYPE_IPV4: u16 = 0x0800;
const ETHERTYPE_ARP: u16 = 0x0806;
const ETHERTYPE_VLAN: u16 = 0x8100;

impl Filter {
    pub fn allows(&self, frame: &[u8]) -> bool {
        let Some((ethertype, off)) = eth_payload_offset(frame) else { return false };
        match ethertype {
            ETHERTYPE_IPV4 => dest_ipv4(frame, off).is_some_and(|ip| self.matches(ip)),
            ETHERTYPE_ARP => match arp_addresses(frame, off) {
                Some((sender, target)) => self.matches(sender) || self.matches(target),
                None => false,
            },
            _ => false,
        }
    }

    fn matches(&self, ip: [u8; 4]) -> bool {
        self.rules.iter().any(|r| r.dst.contains(ip))
    }
}

/// The EtherType and the byte offset of its payload, skipping at most one
/// 802.1Q tag. `None` for anything shorter than a full Ethernet header.
fn eth_payload_offset(frame: &[u8]) -> Option<(u16, usize)> {
    if frame.len() < 14 {
        return None;
    }
    let mut off = 12;
    let mut ethertype = u16::from_be_bytes([frame[off], frame[off + 1]]);
    off += 2;
    if ethertype == ETHERTYPE_VLAN {
        if frame.len() < off + 4 {
            return None;
        }
        ethertype = u16::from_be_bytes([frame[off + 2], frame[off + 3]]);
        off += 4;
    }
    Some((ethertype, off))
}

/// The destination IPv4 address, given an IPv4 EtherType payload offset.
fn dest_ipv4(frame: &[u8], off: usize) -> Option<[u8; 4]> {
    if frame.len() < off + 20 {
        return None;
    }
    let ip = &frame[off..];
    Some([ip[16], ip[17], ip[18], ip[19]])
}

/// `(sender_ip, target_ip)` of an ARP-for-IPv4-over-Ethernet packet, given
/// its payload offset. `None` for anything else (IPv6 NDP uses a different
/// EtherType entirely, so this never needs to reject that case here).
fn arp_addresses(frame: &[u8], off: usize) -> Option<([u8; 4], [u8; 4])> {
    if frame.len() < off + 8 {
        return None;
    }
    let proto_type = u16::from_be_bytes([frame[off + 2], frame[off + 3]]);
    let hw_len = frame[off + 4] as usize;
    let proto_len = frame[off + 5] as usize;
    if proto_type != ETHERTYPE_IPV4 || proto_len != 4 {
        return None;
    }
    let sender_ip_off = off + 8 + hw_len;
    let target_ip_off = sender_ip_off + proto_len + hw_len;
    if frame.len() < target_ip_off + proto_len {
        return None;
    }
    let at = |o: usize| [frame[o], frame[o + 1], frame[o + 2], frame[o + 3]];
    Some((at(sender_ip_off), at(target_ip_off)))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ipv4_frame(dst_ip: [u8; 4]) -> Vec<u8> {
        let mut f = vec![0u8; 34];
        f[12] = 0x08;
        f[13] = 0x00; // EtherType IPv4
        f[14] = 0x45; // version/IHL
        f[30..34].copy_from_slice(&dst_ip);
        f
    }

    /// A standard Ethernet/IPv4 ARP packet (hw_len=6, proto_len=4): request
    /// if `opcode` is 1, reply if 2. `target_mac` is all-zero for a request,
    /// as a real one would be.
    fn arp_frame(opcode: u16, sender_mac: [u8; 6], sender_ip: [u8; 4], target_mac: [u8; 6], target_ip: [u8; 4]) -> Vec<u8> {
        let mut f = vec![0u8; 14 + 28];
        f[12] = 0x08;
        f[13] = 0x06; // EtherType ARP
        let a = &mut f[14..];
        a[0..2].copy_from_slice(&1u16.to_be_bytes()); // hw type: Ethernet
        a[2..4].copy_from_slice(&ETHERTYPE_IPV4.to_be_bytes());
        a[4] = 6; // hw len
        a[5] = 4; // proto len
        a[6..8].copy_from_slice(&opcode.to_be_bytes());
        a[8..14].copy_from_slice(&sender_mac);
        a[14..18].copy_from_slice(&sender_ip);
        a[18..24].copy_from_slice(&target_mac);
        a[24..28].copy_from_slice(&target_ip);
        f
    }

    fn filter_for(subnet: &str) -> Filter {
        Filter { rules: vec![Rule { dst: Ipv4Net::parse(subnet).unwrap() }] }
    }

    #[test]
    fn matches_configured_subnet() {
        let filter = filter_for("10.0.0.0/24");
        assert!(filter.allows(&ipv4_frame([10, 0, 0, 42])));
        assert!(!filter.allows(&ipv4_frame([10, 0, 1, 42])));
    }

    #[test]
    fn default_deny_with_no_rules() {
        let filter = Filter::default();
        assert!(!filter.allows(&ipv4_frame([10, 0, 0, 42])));
    }

    #[test]
    fn vlan_tagged_frame_is_still_matched() {
        let mut f = vec![0u8; 38];
        f[12] = 0x81;
        f[13] = 0x00; // 802.1Q
        f[16] = 0x08;
        f[17] = 0x00; // inner EtherType IPv4
        f[18] = 0x45;
        f[34..38].copy_from_slice(&[10, 0, 0, 5]);
        let filter = filter_for("10.0.0.0/24");
        assert!(filter.allows(&f));
    }

    #[test]
    fn arp_request_for_a_host_in_the_bridged_subnet_is_forwarded() {
        // The classic case this exists for: a host on our LAN ARPing for
        // a peer-side address it can't otherwise resolve.
        let filter = filter_for("192.168.44.0/24");
        let req = arp_frame(1, [0, 1, 2, 3, 4, 5], [192, 168, 44, 3], [0; 6], [192, 168, 44, 1]);
        assert!(filter.allows(&req));
    }

    #[test]
    fn arp_reply_from_a_host_in_the_bridged_subnet_is_forwarded() {
        let filter = filter_for("192.168.44.0/24");
        let reply = arp_frame(2, [6, 7, 8, 9, 10, 11], [192, 168, 44, 1], [0, 1, 2, 3, 4, 5], [192, 168, 44, 3]);
        assert!(filter.allows(&reply));
    }

    #[test]
    fn arp_for_an_unrelated_subnet_is_not_forwarded() {
        let filter = filter_for("192.168.44.0/24");
        let req = arp_frame(1, [0, 1, 2, 3, 4, 5], [10, 0, 0, 3], [0; 6], [10, 0, 0, 1]);
        assert!(!filter.allows(&req));
    }
}
