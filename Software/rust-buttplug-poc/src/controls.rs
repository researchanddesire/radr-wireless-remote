use crate::{
    esp32_ble::Esp32BleCandidate,
    flow,
    rad_ble::{self, RadBleCandidateSummary, RadBleControlSetting, SharedRadBleState},
};
use buttplug_client::{
    ButtplugClientDevice, ButtplugClientError,
    device::{ClientDeviceCommandValue, ClientDeviceFeature, ClientDeviceOutputCommand},
};
use buttplug_core::message::{DeviceFeatureOutput, OutputType};
use strum::IntoEnumIterator;

#[derive(Clone)]
pub struct ActiveOutputControl {
    pub feature: ClientDeviceFeature,
    pub output_type: OutputType,
    pub label: String,
    pub value_percent: i16,
    pub minimum_percent: i16,
}

pub fn output_controls(device: &ButtplugClientDevice) -> Vec<ActiveOutputControl> {
    let mut controls = Vec::new();
    for feature in device.device_features().values() {
        for output_type in OutputType::iter() {
            if !feature.feature().contains_output(output_type) {
                continue;
            }
            let output_name = output_type.to_string();
            let description = feature.feature().description().trim();
            let label = if description.is_empty() {
                format!("Feature {} · {output_name}", feature.feature_index() + 1)
            } else {
                format!("{description} · {output_name}")
            };
            let minimum_percent = feature
                .feature()
                .get_output_limits(output_type)
                .filter(|limits| limits.step_limit().start() < 0)
                .map_or(0, |_| -100);
            controls.push(ActiveOutputControl {
                minimum_percent,
                feature: feature.clone(),
                output_type,
                label,
                value_percent: 0,
            });
        }
    }
    controls
}

pub fn publish_controls(
    state: &SharedRadBleState,
    controls: &[ActiveOutputControl],
    selected: usize,
) {
    rad_ble::with_state(state, |state| {
        state.selected_control_index = controls.get(selected).map(|_| selected);
        state.controls = controls
            .iter()
            .map(|control| RadBleControlSetting {
                feature_index: control.feature.feature_index(),
                label: control.label.clone(),
                output_type: control.output_type.to_string(),
                value_percent: control.value_percent,
                minimum_percent: control.minimum_percent,
            })
            .collect();
        state.control_revision = state.control_revision.wrapping_add(1);
    });
}

pub fn select_control(
    state: &SharedRadBleState,
    controls: &[ActiveOutputControl],
    selected: &mut usize,
    delta: i32,
) {
    if controls.is_empty() {
        return;
    }
    *selected = flow::wrap_selection(*selected, delta, controls.len());
    publish_controls(state, controls, *selected);
    log::info!(
        "Selected upstream control {} of {}: {}",
        *selected + 1,
        controls.len(),
        controls[*selected].label
    );
}

fn log_selected_candidate(candidates: &[Esp32BleCandidate], selected: usize) {
    if let Some(candidate) = candidates.get(selected) {
        log::info!(
            "Selected BLE candidate {} of {}: {:?} {} protocols={:?}",
            selected + 1,
            candidates.len(),
            candidate.name,
            candidate.address,
            candidate.protocols
        );
    }
}

pub fn publish_selection(
    state: &SharedRadBleState,
    candidates: &[Esp32BleCandidate],
    selected: usize,
) {
    rad_ble::with_state(state, |state| {
        state.candidate_count = candidates.len();
        state.candidates = candidates
            .iter()
            .map(|candidate| RadBleCandidateSummary {
                name: candidate.display_name.clone(),
                advertised_name: candidate.name.clone(),
                address: candidate.address.clone(),
                protocols: candidate.protocols.clone(),
            })
            .collect();
        state.selected_index = candidates.get(selected).map(|_| selected);
        state.selected_name = candidates
            .get(selected)
            .map(|candidate| candidate.display_name.clone());
        state.selected_protocols = candidates
            .get(selected)
            .map(|candidate| candidate.protocols.clone())
            .unwrap_or_default();
    });
}

pub fn select_candidate(
    state: &SharedRadBleState,
    candidates: &[Esp32BleCandidate],
    selected: &mut usize,
    delta: i32,
) {
    if candidates.is_empty() {
        return;
    }
    *selected = flow::wrap_selection(*selected, delta, candidates.len());
    log_selected_candidate(candidates, *selected);
    publish_selection(state, candidates, *selected);
}

impl ActiveOutputControl {
    pub fn command(&self, percent: i16) -> Result<ClientDeviceOutputCommand, ButtplugClientError> {
        let limits = self
            .feature
            .feature()
            .get_output_limits(self.output_type)
            .expect("enumerated output must have limits");
        let range = limits.step_limit();
        // Signed ranges expose both rotation directions. Use upstream limits, including
        // asymmetric ranges, rather than assuming every actuator has 0..100 steps.
        let extent = if percent < 0 {
            -i64::from(range.start())
        } else {
            i64::from(range.end())
        };
        let steps = (i64::from(percent) * extent / 100)
            .clamp(i64::from(range.start()), i64::from(range.end())) as i32;
        let value = ClientDeviceCommandValue::Steps(steps);
        if let Some(DeviceFeatureOutput::HwPositionWithDuration(properties)) =
            self.feature.feature().get_output(self.output_type)
        {
            let duration = 500_i32
                .clamp(properties.duration().start(), properties.duration().end())
                .max(0) as u32;
            Ok(ClientDeviceOutputCommand::HwPositionWithDuration(
                value, duration,
            ))
        } else {
            ClientDeviceOutputCommand::from_command_value(self.output_type, &value)
        }
    }
}
