use crate::screen::{RadBleCandidateSummary, RadBleControlSetting, RadBleScreenState};
use serde_json::Value;
use std::sync::{Arc, Mutex};

#[derive(Debug)]
pub struct RadBleRuntimeState {
    pub phase: String,
    pub status: String,
    pub error_code: Option<String>,
    pub elapsed_seconds: u64,
    pub connection_generation: u32,
    pub candidate_count: usize,
    pub selected_index: Option<usize>,
    pub selected_name: Option<String>,
    pub selected_protocols: Vec<String>,
    pub candidates: Vec<RadBleCandidateSummary>,
    pub connected_name: Option<String>,
    pub feature_count: usize,
    pub features: Vec<Value>,
    pub selected_control_index: Option<usize>,
    pub controls: Vec<RadBleControlSetting>,
    pub control_revision: u32,
    pub last_connected_name: Option<String>,
    pub last_feature_count: usize,
    pub last_features: Vec<Value>,
    pub last_error: Option<String>,
    pub framebuffer_generation: u32,
    pub framebuffer_hash: u32,
    pub framebuffer_rle: Vec<u8>,
    pub lease_active: bool,
}

impl Default for RadBleRuntimeState {
    fn default() -> Self {
        Self {
            phase: "starting".to_owned(),
            status: "Getting ready".to_owned(),
            error_code: None,
            elapsed_seconds: 0,
            connection_generation: 0,
            candidate_count: 0,
            selected_index: None,
            selected_name: None,
            selected_protocols: Vec::new(),
            candidates: Vec::new(),
            connected_name: None,
            feature_count: 0,
            features: Vec::new(),
            selected_control_index: None,
            controls: Vec::new(),
            control_revision: 0,
            last_connected_name: None,
            last_feature_count: 0,
            last_features: Vec::new(),
            last_error: None,
            framebuffer_generation: 0,
            framebuffer_hash: 0,
            framebuffer_rle: Vec::new(),
            lease_active: false,
        }
    }
}

pub type SharedRadBleState = Arc<Mutex<RadBleRuntimeState>>;

pub fn shared_state() -> SharedRadBleState {
    Arc::new(Mutex::new(RadBleRuntimeState::default()))
}

pub fn with_state(state: &SharedRadBleState, update: impl FnOnce(&mut RadBleRuntimeState)) {
    match state.lock() {
        Ok(mut state) => update(&mut state),
        Err(poisoned) => update(&mut poisoned.into_inner()),
    }
}

pub fn screen_state(state: &SharedRadBleState) -> RadBleScreenState {
    let state = read_state(state);
    RadBleScreenState {
        phase: state.phase.clone(),
        status: state.status.clone(),
        error_code: state.error_code.clone(),
        elapsed_seconds: state.elapsed_seconds,
        connection_generation: state.connection_generation,
        candidate_count: state.candidate_count,
        selected_index: state.selected_index,
        selected_name: state.selected_name.clone(),
        selected_protocols: state.selected_protocols.clone(),
        candidates: state.candidates.clone(),
        connected_name: state.connected_name.clone(),
        feature_count: state.feature_count,
        selected_control_index: state.selected_control_index,
        controls: state.controls.clone(),
        control_revision: state.control_revision,
        last_error: state.last_error.clone(),
    }
}

pub fn set_framebuffer(state: &SharedRadBleState, pixels: &[u16]) {
    let mut raw_hash = 0x811c_9dc5_u32;
    let mut rle = Vec::new();
    let mut cursor = 0;
    while cursor < pixels.len() {
        let color = pixels[cursor];
        let mut count = 1_usize;
        while cursor + count < pixels.len()
            && pixels[cursor + count] == color
            && count < usize::from(u16::MAX)
        {
            count += 1;
        }
        rle.extend_from_slice(&(count as u16).to_le_bytes());
        rle.extend_from_slice(&color.to_le_bytes());
        for pixel in &pixels[cursor..cursor + count] {
            for byte in pixel.to_le_bytes() {
                raw_hash ^= u32::from(byte);
                raw_hash = raw_hash.wrapping_mul(0x0100_0193);
            }
        }
        cursor += count;
    }
    with_state(state, |state| {
        state.framebuffer_generation = state.framebuffer_generation.wrapping_add(1);
        state.framebuffer_hash = raw_hash;
        state.framebuffer_rle = rle;
    });
}

pub fn read_state(state: &SharedRadBleState) -> std::sync::MutexGuard<'_, RadBleRuntimeState> {
    state
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}
