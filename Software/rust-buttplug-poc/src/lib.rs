//! Host-testable lifecycle, catalog labels, and the exact firmware renderer.
pub mod ble_policy;
pub mod device_names;
#[cfg(all(test, not(target_os = "espidf")))]
#[path = "../tests/mock_transport.rs"]
mod esp32_ble;
pub mod flow;
pub mod runtime_state;
pub mod screen;
pub mod ui;
#[cfg(all(test, not(target_os = "espidf")))]
mod rad_ble {
    pub use crate::runtime_state::*;
    pub use crate::screen::{RadBleCandidateSummary, RadBleControlSetting};
    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    pub enum RadBleControlEvent {
        Left,
        Middle { defer_ms: u32 },
        Right,
        EncoderLeft { delta: i32 },
        EncoderRight { delta: i32 },
        Reset,
    }
}
#[cfg(all(test, not(target_os = "espidf")))]
mod controls;
#[cfg(all(test, not(target_os = "espidf")))]
use controls::{
    ActiveOutputControl, output_controls, publish_controls, publish_selection, select_candidate,
    select_control,
};
#[cfg(all(test, not(target_os = "espidf")))]
const TEST_AUTO_APPROVE_NAME: Option<&str> = None;
#[cfg(all(test, not(target_os = "espidf")))]
#[path = "../tests/catalog_and_ui.rs"]
mod catalog_and_ui;
#[cfg(all(test, not(target_os = "espidf")))]
mod controller;
