//! Transport policy shared by the ESP32 implementation and host regression tests.
pub fn write_response(
    requested: bool,
    can_write: bool,
    can_write_without_response: bool,
) -> Option<bool> {
    match (requested, can_write, can_write_without_response) {
        (true, true, _) | (false, true, false) => Some(true),
        (_, _, true) => Some(false),
        _ => None,
    }
}

/// NimBLE encodes ATT errors as 0x100 + the ATT status. Only rejected security
/// operations can be retried without risking a duplicate actuator command.
pub fn needs_pairing(status: u32) -> bool {
    matches!(status, 0x105 | 0x10c | 0x10f)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn write_modes_follow_properties_with_upstream_compatible_fallback() {
        assert_eq!(write_response(true, true, true), Some(true));
        assert_eq!(write_response(false, true, true), Some(false));
        assert_eq!(write_response(true, false, true), Some(false));
        assert_eq!(write_response(false, true, false), Some(true));
        assert_eq!(write_response(true, false, false), None);
    }
    #[test]
    fn only_explicit_security_rejections_are_retried() {
        for status in [0x105, 0x10c, 0x10f] {
            assert!(needs_pairing(status));
        }
        for status in [0, 0x101, 0x103, 0x106, 0x10e, 0xffff] {
            assert!(!needs_pairing(status));
        }
    }
}
