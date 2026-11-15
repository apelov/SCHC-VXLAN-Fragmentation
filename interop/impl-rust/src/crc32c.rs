//! CRC-32C (Castagnoli) as specified in the Reassembly Check Sequence section
//! of the draft: polynomial 0x1edc6f41 (reflected 0x82f63b78), reflected
//! input/output, initial register and final XOR both 0xffffffff.

use std::sync::OnceLock;

const POLY: u32 = 0x82f63b78;

fn table() -> &'static [u32; 256] {
    static TABLE: OnceLock<[u32; 256]> = OnceLock::new();
    TABLE.get_or_init(|| {
        let mut t = [0u32; 256];
        for (i, entry) in t.iter_mut().enumerate() {
            let mut c = i as u32;
            for _ in 0..8 {
                c = if c & 1 != 0 { POLY ^ (c >> 1) } else { c >> 1 };
            }
            *entry = c;
        }
        t
    })
}

/// CRC-32C over `data`. Result is returned as a plain `u32`; serialize with
/// `to_be_bytes()` per the draft ("most significant byte first").
pub fn crc32c(data: &[u8]) -> u32 {
    let table = table();
    let mut crc = 0xffff_ffffu32;
    for &b in data {
        crc = table[((crc ^ b as u32) & 0xff) as usize] ^ (crc >> 8);
    }
    crc ^ 0xffff_ffff
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn check_value() {
        assert_eq!(crc32c(b"123456789"), 0xe3069283);
    }
}
