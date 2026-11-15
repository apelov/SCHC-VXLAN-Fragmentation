//! Raw socket plumbing for the live VTEP binary: AF_PACKET capture/inject
//! on the LAN interface, and a raw IPv4/UDP receive path on the WAN side
//! so the real UDP checksum field is visible (a plain `UdpSocket` only
//! hands back the payload, and the kernel accepts a zero IPv4 UDP
//! checksum by default -- the draft requires dropping it).
//!
//! This module is Linux-only (AF_PACKET, SOL_PACKET) and needs CAP_NET_RAW
//! (or root) to open its sockets.

use std::ffi::CString;
use std::io;
use std::mem;
use std::os::unix::io::RawFd;

/// Not in the `libc` crate: SOL_PACKET option to read (and reset) this
/// socket's own packets-seen/packets-dropped counters.
const PACKET_STATISTICS: libc::c_int = 6;

/// Not in the `libc` crate: these are `linux/if_tun.h` constants, a
/// different numbering space than the regular `IFF_*` interface flags
/// `libc` does define.
const TUNSETIFF: libc::c_ulong = 0x4004_54ca;
const IFF_TAP: i16 = 0x0002;
const IFF_NO_PI: i16 = 0x1000;

fn check(ret: libc::c_int) -> io::Result<libc::c_int> {
    if ret < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(ret)
    }
}

fn if_index(fd: RawFd, ifname: &str) -> io::Result<libc::c_int> {
    let mut ifr: libc::ifreq = unsafe { mem::zeroed() };
    let name = CString::new(ifname).expect("interface name has no NUL bytes");
    let bytes = name.as_bytes_with_nul();
    assert!(bytes.len() <= ifr.ifr_name.len(), "interface name too long: {ifname}");
    for (dst, &src) in ifr.ifr_name.iter_mut().zip(bytes) {
        *dst = src as libc::c_char;
    }
    unsafe { check(libc::ioctl(fd, libc::SIOCGIFINDEX, &mut ifr))? };
    Ok(unsafe { ifr.ifr_ifru.ifru_ifindex })
}

fn set_promiscuous(fd: RawFd, ifname: &str, on: bool) -> io::Result<()> {
    let mut ifr: libc::ifreq = unsafe { mem::zeroed() };
    let name = CString::new(ifname).unwrap();
    let bytes = name.as_bytes_with_nul();
    for (dst, &src) in ifr.ifr_name.iter_mut().zip(bytes) {
        *dst = src as libc::c_char;
    }
    unsafe { check(libc::ioctl(fd, libc::SIOCGIFFLAGS, &mut ifr))? };
    let flags = unsafe { ifr.ifr_ifru.ifru_flags };
    let flags = if on { flags | (libc::IFF_PROMISC as i16) } else { flags & !(libc::IFF_PROMISC as i16) };
    unsafe {
        ifr.ifr_ifru.ifru_flags = flags;
        check(libc::ioctl(fd, libc::SIOCSIFFLAGS, &mut ifr))?;
    }
    Ok(())
}

/// One end of the LAN-side AF_PACKET socket: captures (and can inject)
/// whole Ethernet frames on `ifname`, promiscuously so frames not
/// addressed to this host's own MAC are still seen.
pub struct PacketSocket {
    fd: RawFd,
    ifindex: libc::c_int,
}

impl PacketSocket {
    pub fn open(ifname: &str) -> io::Result<Self> {
        let fd = unsafe {
            check(libc::socket(
                libc::AF_PACKET,
                libc::SOCK_RAW,
                (libc::ETH_P_ALL as u16).to_be() as libc::c_int,
            ))?
        };
        let ifindex = if_index(fd, ifname)?;

        let mut addr: libc::sockaddr_ll = unsafe { mem::zeroed() };
        addr.sll_family = libc::AF_PACKET as u16;
        addr.sll_protocol = (libc::ETH_P_ALL as u16).to_be();
        addr.sll_ifindex = ifindex;
        unsafe {
            check(libc::bind(
                fd,
                &addr as *const _ as *const libc::sockaddr,
                mem::size_of::<libc::sockaddr_ll>() as u32,
            ))?;
        }

        set_promiscuous(fd, ifname, true)?;

        // Deliberately NOT setting PACKET_IGNORE_OUTGOING here: the kernel
        // marks a frame "outgoing" the same way whether it came from this
        // process re-injecting a reassembled frame, or from this host's
        // own IP stack originating genuinely new traffic (e.g. a local
        // ping/iperf3 run on the VTEP host itself) -- it can't tell those
        // apart, and we want to capture the second kind. Loop prevention
        // for the first kind is instead a content-fingerprint check in
        // `bin/vtep.rs`, around every `PacketSocket::send`.

        // Promiscuous capture at line rate can easily outrun the kernel
        // default receive buffer for this socket -- same class of problem
        // as the raw WAN socket's, just on the LAN side. Best-effort: the
        // kernel caps this at net.core.rmem_max regardless of the request.
        let rc = unsafe {
            libc::setsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_RCVBUF,
                &WAN_RX_BUFFER_BYTES as *const _ as *const libc::c_void,
                mem::size_of::<libc::c_int>() as u32,
            )
        };
        if rc < 0 {
            eprintln!(
                "warning: setsockopt(SO_RCVBUF, {WAN_RX_BUFFER_BYTES}) on {ifname} failed: {}",
                io::Error::last_os_error()
            );
        }

        Ok(Self { fd, ifindex })
    }

    /// The SO_RCVBUF actually in effect.
    pub fn recv_buffer_size(&self) -> io::Result<libc::c_int> {
        let mut value: libc::c_int = 0;
        let mut len = mem::size_of::<libc::c_int>() as u32;
        unsafe {
            check(libc::getsockopt(
                self.fd,
                libc::SOL_SOCKET,
                libc::SO_RCVBUF,
                &mut value as *mut _ as *mut libc::c_void,
                &mut len,
            ))?;
        }
        Ok(value)
    }

    /// `(packets_seen, packets_dropped)` since the last call -- the kernel
    /// resets both counters on each read of `PACKET_STATISTICS`.
    pub fn stats(&self) -> io::Result<(u32, u32)> {
        #[repr(C)]
        #[derive(Default)]
        struct TpacketStats {
            tp_packets: u32,
            tp_drops: u32,
        }
        let mut stats = TpacketStats::default();
        let mut len = mem::size_of::<TpacketStats>() as u32;
        unsafe {
            check(libc::getsockopt(
                self.fd,
                libc::SOL_PACKET,
                PACKET_STATISTICS,
                &mut stats as *mut _ as *mut libc::c_void,
                &mut len,
            ))?;
        }
        Ok((stats.tp_packets, stats.tp_drops))
    }

    /// Block for the next frame (up to 65535 bytes).
    pub fn recv(&self) -> io::Result<Vec<u8>> {
        let mut buf = vec![0u8; 65535];
        let n = unsafe {
            check(libc::recv(self.fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len(), 0) as libc::c_int)?
        };
        buf.truncate(n as usize);
        Ok(buf)
    }

    /// Transmit a complete Ethernet frame (header included) on this interface.
    pub fn send(&self, frame: &[u8]) -> io::Result<()> {
        let mut addr: libc::sockaddr_ll = unsafe { mem::zeroed() };
        addr.sll_family = libc::AF_PACKET as u16;
        addr.sll_protocol = (libc::ETH_P_ALL as u16).to_be();
        addr.sll_ifindex = self.ifindex;
        addr.sll_halen = 6;
        unsafe {
            check(libc::sendto(
                self.fd,
                frame.as_ptr() as *const libc::c_void,
                frame.len(),
                0,
                &addr as *const _ as *const libc::sockaddr,
                mem::size_of::<libc::sockaddr_ll>() as u32,
            ) as libc::c_int)?;
        }
        Ok(())
    }
}

impl Drop for PacketSocket {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.fd);
        }
    }
}

/// A TAP device: unlike `PacketSocket` on a real NIC, a frame written here
/// is delivered into *this host's own* kernel receive path, not just onto
/// a wire. That's what lets a reconstructed frame destined to one of this
/// host's own locally-configured addresses (as opposed to a real external
/// device on the LAN segment) actually resolve/respond -- the same reason
/// the C implementation bridges a TAP (`schc0`) with its physical port
/// instead of injecting raw frames onto the NIC directly. Expects the
/// named TAP to already exist (persistent, created and bridged by the
/// host's own network setup) and just attaches to it.
pub struct TapDevice {
    fd: RawFd,
}

impl TapDevice {
    pub fn open(ifname: &str) -> io::Result<Self> {
        let path = CString::new("/dev/net/tun").unwrap();
        let fd = unsafe { check(libc::open(path.as_ptr(), libc::O_RDWR | libc::O_CLOEXEC))? };

        let mut req: libc::ifreq = unsafe { mem::zeroed() };
        let name = CString::new(ifname).expect("interface name has no NUL bytes");
        let bytes = name.as_bytes_with_nul();
        assert!(bytes.len() <= req.ifr_name.len(), "interface name too long: {ifname}");
        for (dst, &src) in req.ifr_name.iter_mut().zip(bytes) {
            *dst = src as libc::c_char;
        }
        req.ifr_ifru.ifru_flags = IFF_TAP | IFF_NO_PI;
        if let Err(e) = unsafe { check(libc::ioctl(fd, TUNSETIFF, &mut req)) } {
            unsafe {
                libc::close(fd);
            }
            return Err(e);
        }
        Ok(Self { fd })
    }

    /// Block for the next frame (up to 65535 bytes).
    pub fn recv(&self) -> io::Result<Vec<u8>> {
        let mut buf = vec![0u8; 65535];
        let n = unsafe { check(libc::read(self.fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) as libc::c_int)? };
        buf.truncate(n as usize);
        Ok(buf)
    }

    /// Transmit a complete Ethernet frame (header included).
    pub fn send(&self, frame: &[u8]) -> io::Result<()> {
        let n = unsafe { check(libc::write(self.fd, frame.as_ptr() as *const libc::c_void, frame.len()) as libc::c_int)? };
        if n as usize != frame.len() {
            return Err(io::Error::new(io::ErrorKind::Other, "short TAP write"));
        }
        Ok(())
    }
}

impl Drop for TapDevice {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.fd);
        }
    }
}

/// The LAN-side device, chosen automatically by what `ifname` actually is:
/// a TAP device (if `/sys/class/net/<ifname>/tun_flags` exists) is opened
/// as a `TapDevice`, so delivery to this host's own addresses works;
/// anything else is opened as a `PacketSocket` (promiscuous AF_PACKET on a
/// real NIC), as before. Same `recv`/`send` surface either way, so callers
/// don't need to care which one they got.
pub enum LanIo {
    Packet(PacketSocket),
    Tap(TapDevice),
}

impl LanIo {
    pub fn open(ifname: &str) -> io::Result<Self> {
        if std::path::Path::new(&format!("/sys/class/net/{ifname}/tun_flags")).exists() {
            Ok(LanIo::Tap(TapDevice::open(ifname)?))
        } else {
            Ok(LanIo::Packet(PacketSocket::open(ifname)?))
        }
    }

    pub fn recv(&self) -> io::Result<Vec<u8>> {
        match self {
            LanIo::Packet(p) => p.recv(),
            LanIo::Tap(t) => t.recv(),
        }
    }

    pub fn send(&self, frame: &[u8]) -> io::Result<()> {
        match self {
            LanIo::Packet(p) => p.send(frame),
            LanIo::Tap(t) => t.send(frame),
        }
    }

    /// `Ok(0)` for a TAP: SO_RCVBUF doesn't apply to it the way it does to
    /// a socket.
    pub fn recv_buffer_size(&self) -> io::Result<libc::c_int> {
        match self {
            LanIo::Packet(p) => p.recv_buffer_size(),
            LanIo::Tap(_) => Ok(0),
        }
    }

    /// `Ok((0, 0))` for a TAP: it has no PACKET_STATISTICS-equivalent exposed here.
    pub fn stats(&self) -> io::Result<(u32, u32)> {
        match self {
            LanIo::Packet(p) => p.stats(),
            LanIo::Tap(_) => Ok((0, 0)),
        }
    }
}

/// A raw IPv4 UDP receive socket: every incoming UDP datagram to this
/// host, IP header included, so the real checksum field can be checked
/// (see [`crate::net::udp_checksum_valid`]). The caller filters by
/// destination port and address itself.
pub struct RawUdpSocket {
    fd: RawFd,
}

/// One received UDP datagram on the raw path.
pub struct RawUdpDatagram {
    pub src: [u8; 4],
    pub dst: [u8; 4],
    pub src_port: u16,
    pub dst_port: u16,
    pub checksum: u16,
    pub payload: Vec<u8>,
    /// UDP header (checksum field zeroed) followed by payload, ready to
    /// feed straight into [`udp_checksum_valid`].
    pub udp_for_checksum: Vec<u8>,
}

/// Requested SO_RCVBUF for the raw UDP receive socket. This socket sees
/// every UDP datagram delivered to the host, not just ours (that's
/// inherent to a raw IPPROTO_UDP socket), so on a busy host the kernel
/// default buffer overflows fast -- visible as `Udp: RcvbufErrors` in
/// `/proc/net/snmp`, indistinguishable from real network loss unless you
/// go look. The kernel still caps this at `net.core.rmem_max`; actual
/// buffer size is applied, not necessarily this value.
const WAN_RX_BUFFER_BYTES: libc::c_int = 8 * 1024 * 1024;

impl RawUdpSocket {
    pub fn open() -> io::Result<Self> {
        let fd = unsafe { check(libc::socket(libc::AF_INET, libc::SOCK_RAW, libc::IPPROTO_UDP))? };
        let rc = unsafe {
            libc::setsockopt(
                fd,
                libc::SOL_SOCKET,
                libc::SO_RCVBUF,
                &WAN_RX_BUFFER_BYTES as *const _ as *const libc::c_void,
                mem::size_of::<libc::c_int>() as u32,
            )
        };
        if rc < 0 {
            eprintln!(
                "warning: setsockopt(SO_RCVBUF, {WAN_RX_BUFFER_BYTES}) failed: {} -- leaving the kernel default, which may overflow under load",
                io::Error::last_os_error()
            );
        }
        Ok(Self { fd })
    }

    /// The SO_RCVBUF actually in effect (the kernel doubles what's
    /// requested for bookkeeping, and caps it at `net.core.rmem_max`).
    pub fn recv_buffer_size(&self) -> io::Result<libc::c_int> {
        let mut value: libc::c_int = 0;
        let mut len = mem::size_of::<libc::c_int>() as u32;
        unsafe {
            check(libc::getsockopt(
                self.fd,
                libc::SOL_SOCKET,
                libc::SO_RCVBUF,
                &mut value as *mut _ as *mut libc::c_void,
                &mut len,
            ))?;
        }
        Ok(value)
    }

    pub fn recv(&self) -> io::Result<Option<RawUdpDatagram>> {
        let mut buf = vec![0u8; 65535];
        let n = unsafe {
            check(libc::recv(self.fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len(), 0) as libc::c_int)?
        } as usize;
        buf.truncate(n);
        Ok(parse_ipv4_udp(&buf))
    }
}

impl Drop for RawUdpSocket {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.fd);
        }
    }
}

fn parse_ipv4_udp(packet: &[u8]) -> Option<RawUdpDatagram> {
    if packet.len() < 20 {
        return None;
    }
    let version = packet[0] >> 4;
    if version != 4 {
        return None;
    }
    let ihl = (packet[0] & 0x0f) as usize * 4;
    if packet.len() < ihl + 8 || packet[9] != 17 {
        return None; // not UDP
    }
    let src = [packet[12], packet[13], packet[14], packet[15]];
    let dst = [packet[16], packet[17], packet[18], packet[19]];
    let udp = &packet[ihl..];
    let src_port = u16::from_be_bytes([udp[0], udp[1]]);
    let dst_port = u16::from_be_bytes([udp[2], udp[3]]);
    let udp_len = u16::from_be_bytes([udp[4], udp[5]]) as usize;
    let checksum = u16::from_be_bytes([udp[6], udp[7]]);
    if udp.len() < udp_len || udp_len < 8 {
        return None;
    }
    let payload = udp[8..udp_len].to_vec();
    let mut udp_for_checksum = udp[..udp_len].to_vec();
    udp_for_checksum[6] = 0;
    udp_for_checksum[7] = 0;
    Some(RawUdpDatagram { src, dst, src_port, dst_port, checksum, payload, udp_for_checksum })
}

/// Validate a received UDP/IPv4 checksum against the real header. The
/// draft requires dropping a zero checksum, overriding the usual IPv4
/// "no checksum" meaning.
pub fn udp_checksum_valid(src: [u8; 4], dst: [u8; 4], udp_header_and_payload_with_checksum_zeroed: &[u8], claimed: u16) -> bool {
    if claimed == 0 {
        return false;
    }
    let computed = udp_checksum(src, dst, udp_header_and_payload_with_checksum_zeroed);
    // RFC 768: 0x0000 means "no checksum", so a sender whose real checksum
    // computes to exactly zero MUST transmit 0xffff instead. Comparing the
    // wire value directly, without this substitution, would reject a
    // genuinely correct checksum about 1 time in 65536.
    computed == claimed || (computed == 0 && claimed == 0xffff)
}

/// The UDP checksum that should appear on the wire for this datagram
/// (header's checksum field must be zeroed in the input already).
pub fn udp_checksum(src: [u8; 4], dst: [u8; 4], udp_header_and_payload_with_checksum_zeroed: &[u8]) -> u16 {
    let mut pseudo = Vec::with_capacity(12);
    pseudo.extend_from_slice(&src);
    pseudo.extend_from_slice(&dst);
    pseudo.push(0);
    pseudo.push(17);
    pseudo.extend_from_slice(&(udp_header_and_payload_with_checksum_zeroed.len() as u16).to_be_bytes());

    let mut sum = ones_complement_sum(&pseudo);
    sum += ones_complement_sum(udp_header_and_payload_with_checksum_zeroed);
    fold(sum)
}

fn ones_complement_sum(data: &[u8]) -> u32 {
    let mut sum: u32 = 0;
    let mut i = 0;
    while i + 1 < data.len() {
        sum += u16::from_be_bytes([data[i], data[i + 1]]) as u32;
        i += 2;
    }
    if i < data.len() {
        sum += (data[i] as u32) << 8;
    }
    sum
}

fn fold(mut sum: u32) -> u16 {
    while sum >> 16 != 0 {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    !(sum as u16)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checksum_matches_a_known_good_udp_datagram() {
        // A real UDP/IPv4 datagram's checksum, verified by recomputing it
        // the same way the kernel would.
        let src = [192, 168, 11, 1];
        let dst = [192, 168, 11, 2];
        let payload = b"hello schc";
        let mut udp = Vec::new();
        udp.extend_from_slice(&60789u16.to_be_bytes()); // src port
        udp.extend_from_slice(&60789u16.to_be_bytes()); // dst port
        udp.extend_from_slice(&((8 + payload.len()) as u16).to_be_bytes());
        udp.extend_from_slice(&[0, 0]); // checksum placeholder
        udp.extend_from_slice(payload);

        let mut pseudo = Vec::new();
        pseudo.extend_from_slice(&src);
        pseudo.extend_from_slice(&dst);
        pseudo.push(0);
        pseudo.push(17);
        pseudo.extend_from_slice(&(udp.len() as u16).to_be_bytes());
        let mut sum = ones_complement_sum(&pseudo);
        sum += ones_complement_sum(&udp);
        let checksum = fold(sum);

        assert!(udp_checksum_valid(src, dst, &udp, checksum));
        assert!(!udp_checksum_valid(src, dst, &udp, 0));
        assert!(!udp_checksum_valid(src, dst, &udp, checksum ^ 1));
    }

    #[test]
    fn rfc768_zero_checksum_is_transmitted_as_0xffff() {
        // Find a one-byte payload whose real UDP checksum computes to
        // exactly zero, to exercise the RFC 768 substitution rule: the
        // sender transmits 0xffff on the wire, not 0x0000 (reserved to
        // mean "no checksum"), so a receiver must accept 0xffff as valid
        // when its own recomputation is zero.
        let src = [10, 0, 0, 1];
        let dst = [10, 0, 0, 2];
        let (udp, computed_before_substitution) = (0u32..=0xffff)
            .find_map(|w| {
                let mut udp = Vec::new();
                udp.extend_from_slice(&1234u16.to_be_bytes());
                udp.extend_from_slice(&5678u16.to_be_bytes());
                udp.extend_from_slice(&10u16.to_be_bytes());
                udp.extend_from_slice(&[0, 0]);
                udp.extend_from_slice(&(w as u16).to_be_bytes());
                let mut pseudo = Vec::new();
                pseudo.extend_from_slice(&src);
                pseudo.extend_from_slice(&dst);
                pseudo.push(0);
                pseudo.push(17);
                pseudo.extend_from_slice(&(udp.len() as u16).to_be_bytes());
                let mut sum = ones_complement_sum(&pseudo);
                sum += ones_complement_sum(&udp);
                let computed = fold(sum);
                (computed == 0).then_some((udp, computed))
            })
            .expect("at least one byte value should yield a zero checksum");
        assert_eq!(computed_before_substitution, 0);

        assert!(udp_checksum_valid(src, dst, &udp, 0xffff));
        assert!(!udp_checksum_valid(src, dst, &udp, 0));
    }
}
