//! Rust implementation of the SCHC No-ACK VXLAN fragmentation profile
//! (`draft/revised/draft-vladyka-schc-vxlan-fragmentation-00-revised.md`).
//! See `../README.md` (interop plan) for scope and independence rules.

pub mod crc32c;
pub mod filter;
#[cfg(target_os = "linux")]
pub mod net;
pub mod receiver;
pub mod sender;
pub mod wire;
