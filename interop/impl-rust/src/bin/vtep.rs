//! Live VTEP daemon: bridges Ethernet frames between a LAN interface and a
//! peer over the SCHC No-ACK VXLAN fragmentation profile on a WAN
//! interface.
//!
//! Only frames whose inner IPv4 destination matches a configured
//! destination-subnet rule are tunneled (see `schcvx::filter`); everything
//! else is left alone. Configure via `--config FILE` (see
//! `vtep.conf.example` in this crate) and/or `--flag value` arguments,
//! which override anything the file set.
//!
//! Needs CAP_NET_RAW (or root) for the AF_PACKET and raw-IP sockets.

use std::collections::HashMap;
use std::hash::{Hash, Hasher};
use std::net::{Ipv4Addr, UdpSocket};
use std::process;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

/// `YYYY-MM-DDTHH:MM:SS.mmmZ`, UTC. Used to prefix every log line below.
fn timestamp() -> String {
    let since_epoch = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    let secs = since_epoch.as_secs() as libc::time_t;
    let millis = since_epoch.subsec_millis();
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    unsafe {
        libc::gmtime_r(&secs, &mut tm);
    }
    format!(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        millis
    )
}

/// Like `eprintln!`, but every line is prefixed with a UTC timestamp.
macro_rules! log {
    ($($arg:tt)*) => {
        eprintln!("{} {}", timestamp(), format!($($arg)*))
    };
}

use schcvx::filter::{Filter, Ipv4Net, Rule};
use schcvx::net::{udp_checksum, udp_checksum_valid, LanIo, RawUdpSocket};
use schcvx::receiver::{Counters, DropReason, Outcome, OrderedReceiver, TolerantReceiver};
use schcvx::sender::{send, SendConfig};
use schcvx::wire::{classify, Classification, Variant};

struct Config {
    lan_if: String,
    local: Ipv4Addr,
    peer: Ipv4Addr,
    port: u16,
    pmtu: usize,
    h_ip: usize,
    variant: Variant,
    fr_rule: u64,
    vni: u32,
    allow: Vec<Ipv4Net>,
    receiver_mode: ReceiverMode,
    inactivity: Duration,
    deadline: Duration,
    retirement: Duration,
    max_entries: usize,
    /// Per-packet "lan->wan"/"wan->lan delivered" lines, off by default --
    /// each one costs a timestamp syscall + format + write, which matters
    /// at line rate. Drop/error logging (rare in normal operation) is
    /// never gated by this.
    trace: bool,
}

#[derive(Clone, Copy)]
enum ReceiverMode {
    Ordered,
    Tolerant,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            lan_if: "veth-schc0".to_string(),
            local: Ipv4Addr::new(192, 168, 11, 1),
            peer: Ipv4Addr::new(192, 168, 11, 2),
            port: 60789,
            pmtu: 1280,
            h_ip: 20,
            variant: Variant::Compact,
            fr_rule: 1,
            vni: 100,
            allow: Vec::new(),
            receiver_mode: ReceiverMode::Ordered,
            inactivity: Duration::from_millis(10),
            deadline: Duration::from_millis(10),
            retirement: Duration::from_millis(100),
            max_entries: 64,
            trace: false,
        }
    }
}

impl Config {
    /// Apply one `key = value` setting, from either a `--key value` CLI
    /// argument (key given without its leading `--`) or a config file line.
    /// `allow` is additive: each occurrence adds one more forwarding rule
    /// instead of replacing the list, so both the file and the command
    /// line can contribute subnets.
    fn apply(&mut self, key: &str, value: &str) {
        let bad = |what: &str| -> ! { usage_and_exit(&format!("bad {key}: {value} ({what})")) };
        match key {
            "lan-if" => self.lan_if = value.to_string(),
            "local" => self.local = value.parse().unwrap_or_else(|_| bad("not an IPv4 address")),
            "peer" => self.peer = value.parse().unwrap_or_else(|_| bad("not an IPv4 address")),
            "port" => self.port = value.parse().unwrap_or_else(|_| bad("not a port number")),
            "pmtu" => self.pmtu = value.parse().unwrap_or_else(|_| bad("not a number")),
            "h-ip" => self.h_ip = value.parse().unwrap_or_else(|_| bad("not a number")),
            "vni" => self.vni = value.parse().unwrap_or_else(|_| bad("not a number")),
            "rule" => self.fr_rule = value.parse().unwrap_or_else(|_| bad("not a number")),
            "variant" => {
                self.variant = match value {
                    "compact" => Variant::Compact,
                    "extended" => Variant::Extended,
                    _ => bad("must be compact or extended"),
                }
            }
            "receiver" => {
                self.receiver_mode = match value {
                    "ordered" => ReceiverMode::Ordered,
                    "tolerant" => ReceiverMode::Tolerant,
                    _ => bad("must be ordered or tolerant"),
                }
            }
            "allow" => {
                let net = Ipv4Net::parse(value).unwrap_or_else(|| bad("not an ipv4-subnet, e.g. 10.0.0.0/24"));
                self.allow.push(net);
            }
            "inactivity-ms" => self.inactivity = Duration::from_millis(value.parse().unwrap_or_else(|_| bad("not a number"))),
            "deadline-ms" => self.deadline = Duration::from_millis(value.parse().unwrap_or_else(|_| bad("not a number"))),
            "retirement-ms" => self.retirement = Duration::from_millis(value.parse().unwrap_or_else(|_| bad("not a number"))),
            "max-entries" => self.max_entries = value.parse().unwrap_or_else(|_| bad("not a number")),
            "trace" => self.trace = value.parse().unwrap_or_else(|_| bad("must be true or false")),
            other => usage_and_exit(&format!("unknown setting: {other}")),
        }
    }

    /// Load `key = value` lines from a config file (blank lines and lines
    /// starting with `#` are ignored). See `vtep.conf.example` in this
    /// crate for the full set of keys, including the destination-subnet
    /// `allow` rules that decide what gets tunneled.
    fn load_file(&mut self, path: &str) {
        let text = std::fs::read_to_string(path)
            .unwrap_or_else(|e| usage_and_exit(&format!("reading --config {path}: {e}")));
        for (lineno, raw) in text.lines().enumerate() {
            let line = raw.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let Some((key, rest)) = line.split_once('=') else {
                usage_and_exit(&format!("{path}:{}: expected 'key = value', got: {raw}", lineno + 1));
            };
            // Allow a trailing "# ..." comment after the value.
            let value = rest.split('#').next().unwrap_or(rest).trim();
            self.apply(key.trim(), value);
        }
    }

    fn from_args() -> Self {
        let mut cfg = Config::default();
        let mut args = std::env::args().skip(1);
        while let Some(arg) = args.next() {
            let mut next = || args.next().unwrap_or_else(|| usage_and_exit(&format!("missing value for {arg}")));
            match arg.as_str() {
                "-h" | "--help" => usage_and_exit(""),
                "--config" => cfg.load_file(&next()),
                other => match other.strip_prefix("--") {
                    Some(key) => cfg.apply(key, &next()),
                    None => usage_and_exit(&format!("unknown argument: {other}")),
                },
            }
        }
        if cfg.allow.is_empty() {
            usage_and_exit("at least one 'allow' subnet rule is required (default-deny filter) -- via --allow or in a --config file");
        }
        cfg
    }

    fn vxlan_hdr(&self) -> [u8; 8] {
        [0x08, 0, 0, 0, (self.vni >> 16) as u8, (self.vni >> 8) as u8, self.vni as u8, 0]
    }
}

fn usage_and_exit(msg: &str) -> ! {
    if !msg.is_empty() {
        eprintln!("error: {msg}\n");
    }
    eprintln!(
        "usage: vtep [--config FILE] --allow <ipv4-subnet> [--lan-if IF] [--local IP] [--peer IP] [--port N]\n\
         \x20              [--pmtu N] [--h-ip N] [--variant compact|extended] [--rule N] [--vni N]\n\
         \x20              [--receiver ordered|tolerant] [--inactivity-ms N] [--deadline-ms N]\n\
         \x20              [--retirement-ms N] [--max-entries N] [--trace true|false]\n\
         \x20\n\
         \x20       --config loads 'key = value' settings from a file first (see vtep.conf.example);\n\
         \x20       any --flag given after it on the command line overrides that file's value.\n\
         \x20       --allow may repeat, on the command line and/or in the file -- see vtep.conf.example.\n\
         \x20       --trace (default false) logs every forwarded frame; leave off at line rate --\n\
         \x20       drops and errors are always logged regardless."
    );
    process::exit(2);
}

enum AnyReceiver {
    Ordered(OrderedReceiver),
    Tolerant(TolerantReceiver),
}

impl AnyReceiver {
    fn on_fragment(&mut self, data: &[u8], now: Instant) -> Outcome {
        match self {
            AnyReceiver::Ordered(r) => r.on_fragment(data, now),
            AnyReceiver::Tolerant(r) => r.on_fragment(data, now),
        }
    }
    fn sweep(&mut self, now: Instant) {
        match self {
            AnyReceiver::Ordered(r) => {
                r.sweep(now);
            }
            AnyReceiver::Tolerant(r) => {
                r.sweep(now);
            }
        }
    }
    fn counters(&self) -> Counters {
        match self {
            AnyReceiver::Ordered(r) => r.counters,
            AnyReceiver::Tolerant(r) => r.counters,
        }
    }
}

/// Next DTag, wrapped to the configured variant's DTag field width (32
/// bits for Compact, 60 for Extended) -- `wire::encode` rejects a tag that
/// doesn't fit, so this must never hand out a value the variant can't
/// carry. Wrapping (not saturating) is fine: DTag Reuse and Restart still
/// applies, and these lab defaults don't claim restart-safety anyway.
fn next_dtag(counter: &AtomicU64, dtag_bits: u32) -> u64 {
    let mask = if dtag_bits >= 64 { u64::MAX } else { (1u64 << dtag_bits) - 1 };
    counter.fetch_add(1, Ordering::Relaxed) & mask
}

fn random_seed(dtag_bits: u32) -> u64 {
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos() as u64;
    let seed = nanos ^ ((process::id() as u64) << 32);
    let mask = if dtag_bits >= 64 { u64::MAX } else { (1u64 << dtag_bits) - 1 };
    seed & mask
}

fn frame_fingerprint(frame: &[u8]) -> u64 {
    let mut h = std::collections::hash_map::DefaultHasher::new();
    frame.hash(&mut h);
    h.finish()
}

/// How long a just-injected frame's fingerprint is remembered. Must only
/// need to outlast however long the kernel takes to hand this exact frame
/// back to our own promiscuous capture socket, which is sub-millisecond on
/// a local link; generous margin for a slow/loaded host.
const SELF_INJECTED_TTL: Duration = Duration::from_millis(500);

/// Loop prevention for the LAN capture/inject socket: `PacketSocket` (the
/// AF_PACKET variant of `LanIo`) has no
/// kernel-level "ignore my own outgoing frames" filter (see `net.rs` for
/// why), so every frame this process injects onto the LAN is remembered
/// here, briefly, by content fingerprint. The capture loop drops a match
/// instead of re-tunneling it; anything else -- including genuinely new
/// traffic this host's own IP stack originates -- is captured normally.
#[derive(Default)]
struct SelfInjectedFrames(HashMap<u64, Instant>);

impl SelfInjectedFrames {
    fn mark(&mut self, frame: &[u8]) {
        self.0.insert(frame_fingerprint(frame), Instant::now());
    }

    /// True if `frame` matches something injected within the TTL (and
    /// consumes that entry either way it's found, stale or not).
    fn take_is_self_injected(&mut self, frame: &[u8], now: Instant) -> bool {
        match self.0.remove(&frame_fingerprint(frame)) {
            Some(marked_at) => now.duration_since(marked_at) < SELF_INJECTED_TTL,
            None => false,
        }
    }

    /// Drop anything that's aged out without ever being seen again by the
    /// capture loop (e.g. it genuinely left the LAN and nothing echoed it
    /// back) -- otherwise this would grow without bound.
    fn prune(&mut self, now: Instant) {
        self.0.retain(|_, &mut marked_at| now.duration_since(marked_at) < SELF_INJECTED_TTL);
    }
}

fn main() {
    let cfg = Config::from_args();
    let vxlan_hdr = cfg.vxlan_hdr();
    log!(
        "vtep: lan={} wan local={} peer={}:{} variant={:?} rule={} vni={} allow={:?}",
        cfg.lan_if, cfg.local, cfg.peer, cfg.port, cfg.variant, cfg.fr_rule, cfg.vni, cfg.allow
    );

    let lan = Arc::new(LanIo::open(&cfg.lan_if).unwrap_or_else(|e| {
        log!("error: opening LAN socket on {}: {e} (need CAP_NET_RAW/root?)", cfg.lan_if);
        process::exit(1);
    }));
    log!("lan rx socket buffer: {} bytes", lan.recv_buffer_size().unwrap_or(-1));
    let wan_rx = RawUdpSocket::open().unwrap_or_else(|e| {
        log!("error: opening raw WAN receive socket: {e} (need CAP_NET_RAW/root?)");
        process::exit(1);
    });
    log!("wan rx socket buffer: {} bytes (requested {})", wan_rx.recv_buffer_size().unwrap_or(-1), 8 * 1024 * 1024);
    let wan_tx = UdpSocket::bind((cfg.local, cfg.port)).unwrap_or_else(|e| {
        log!("error: binding WAN send socket to {}:{}: {e}", cfg.local, cfg.port);
        process::exit(1);
    });

    let filter = Filter { rules: cfg.allow.iter().map(|&dst| Rule { dst }).collect() };
    let send_cfg = SendConfig { variant: cfg.variant, pmtu: cfg.pmtu, h_ip: cfg.h_ip, fr_rule: cfg.fr_rule };
    let dtag_bits = cfg.variant.dtag_bits();
    let dtag = Arc::new(AtomicU64::new(random_seed(dtag_bits)));

    let receiver = Arc::new(Mutex::new(match cfg.receiver_mode {
        ReceiverMode::Ordered => AnyReceiver::Ordered(OrderedReceiver::new(cfg.variant, cfg.inactivity, cfg.max_entries)),
        ReceiverMode::Tolerant => {
            AnyReceiver::Tolerant(TolerantReceiver::new(cfg.variant, cfg.deadline, Some(cfg.inactivity), cfg.retirement, cfg.max_entries))
        }
    }));
    let self_injected = Arc::new(Mutex::new(SelfInjectedFrames::default()));

    // LAN -> WAN: capture, filter, fragment (if needed) and send.
    {
        let lan = Arc::clone(&lan);
        let dtag = Arc::clone(&dtag);
        let self_injected = Arc::clone(&self_injected);
        let peer = cfg.peer;
        let port = cfg.port;
        let trace = cfg.trace;
        thread::spawn(move || loop {
            let frame = match lan.recv() {
                Ok(f) => f,
                Err(e) => {
                    log!("lan recv error: {e}");
                    continue;
                }
            };
            if self_injected.lock().unwrap().take_is_self_injected(&frame, Instant::now()) {
                continue; // our own reassembled frame looping back, not new traffic
            }
            if !filter.allows(&frame) {
                continue;
            }
            let tag = next_dtag(&dtag, dtag_bits);
            match send(&vxlan_hdr, &frame, tag, &send_cfg) {
                Ok(payloads) => {
                    for p in &payloads {
                        if let Err(e) = wan_tx.send_to(p, (peer, port)) {
                            log!("wan send error: {e}");
                        }
                    }
                    if trace {
                        log!("lan->wan: {} byte frame as {} UDP payload(s), dtag={tag}", frame.len(), payloads.len());
                    }
                }
                Err(e) => log!("lan->wan: frame rejected by sender procedure: {e:?}"),
            }
        });
    }

    // WAN -> LAN: receive, validate, (re)assemble and inject.
    {
        let lan = Arc::clone(&lan);
        let receiver = Arc::clone(&receiver);
        let self_injected = Arc::clone(&self_injected);
        let variant = cfg.variant;
        let port = cfg.port;
        let peer_octets = cfg.peer.octets();
        let trace = cfg.trace;
        thread::spawn(move || loop {
            let dg = match wan_rx.recv() {
                Ok(Some(dg)) => dg,
                Ok(None) => continue,
                Err(e) => {
                    log!("wan recv error: {e}");
                    continue;
                }
            };
            if dg.dst_port != port {
                continue;
            }
            if dg.src != peer_octets {
                continue; // peer admission: not our configured peer
            }
            if !udp_checksum_valid(dg.src, dg.dst, &dg.udp_for_checksum, dg.checksum) {
                log!(
                    "wan<-peer: dropped, bad/zero UDP checksum (claimed={:#06x} computed={:#06x} len={} src={:?} dst={:?})",
                    dg.checksum,
                    udp_checksum(dg.src, dg.dst, &dg.udp_for_checksum),
                    dg.udp_for_checksum.len(),
                    dg.src,
                    dg.dst
                );
                continue;
            }
            let delivered = match classify(variant, &dg.payload) {
                Ok(Classification::Packet(data)) => Some(data.to_vec()),
                Ok(Classification::Fragment(data)) => {
                    let mut r = receiver.lock().unwrap();
                    match r.on_fragment(data, Instant::now()) {
                        Outcome::Delivered(packet) => Some(packet),
                        Outcome::Pending => None,
                        Outcome::Dropped(reason) => {
                            if reason != DropReason::Malformed {
                                log!("wan<-peer: fragment dropped: {reason:?}");
                            }
                            None
                        }
                    }
                }
                Err(e) => {
                    log!("wan<-peer: unparseable datagram: {e:?}");
                    None
                }
            };
            if let Some(packet) = delivered {
                if packet.len() > 9 {
                    let frame = &packet[9..]; // strip C/D RuleID (1) + VXLAN header (8)
                    self_injected.lock().unwrap().mark(frame);
                    if let Err(e) = lan.send(frame) {
                        log!("lan inject error: {e}");
                    } else if trace {
                        log!("wan->lan: delivered {} byte frame", frame.len());
                    }
                }
            }
        });
    }

    // Timer sweep.
    {
        let receiver = Arc::clone(&receiver);
        thread::spawn(move || loop {
            thread::sleep(Duration::from_millis(5));
            receiver.lock().unwrap().sweep(Instant::now());
        });
    }

    // Periodic counters, for manual interop verification.
    loop {
        thread::sleep(Duration::from_secs(5));
        let c = receiver.lock().unwrap().counters();
        log!(
            "counters: completed={} expired={} late={} duplicate={} integrity_failure={} malformed={} table_exhaustion={}",
            c.completed, c.expired, c.late, c.duplicate, c.integrity_failure, c.malformed, c.table_exhaustion
        );
        // PACKET_STATISTICS resets on read, so this is packets/drops since
        // the last line, not a running total.
        match lan.stats() {
            Ok((seen, dropped)) if dropped > 0 => {
                log!("lan capture ({}): {seen} packets seen, {dropped} DROPPED since last report", cfg.lan_if)
            }
            Ok(_) => {}
            Err(e) => log!("lan capture stats error: {e}"),
        }
        self_injected.lock().unwrap().prune(Instant::now());
    }
}
