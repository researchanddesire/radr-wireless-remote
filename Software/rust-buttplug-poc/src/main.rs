use anyhow::Context;
use buttplug_client::{
    ButtplugClient, ButtplugClientDevice, ButtplugClientEvent,
    device::{ClientDeviceCommandValue, ClientDeviceFeature, ClientDeviceOutputCommand},
};
use buttplug_client_in_process::ButtplugInProcessClientConnectorBuilder;
use buttplug_core::message::OutputType;
use buttplug_server::{
    ButtplugServerBuilder,
    device::{ServerDeviceManagerBuilder, get_default_protocol_map},
};
use buttplug_server_device_config::{ProtocolCommunicationSpecifier, load_protocol_configs};
use esp_idf_svc::hal::{
    gpio::{AnyInputPin, InputPin, PinDriver, Pull},
    pcnt::{
        PcntUnitDriver,
        config::{ChannelEdgeAction, ChannelLevelAction, GlitchFilterConfig, UnitConfig},
    },
    peripherals::Peripherals,
};
use futures::StreamExt;
use std::{
    collections::HashSet,
    sync::Arc,
    time::{Duration, Instant},
};
use strum::IntoEnumIterator;

mod esp32_ble;
mod psram_allocator;
mod rad_ble;
mod radr_display;

use esp32_ble::{Esp32BleCandidate, Esp32BleCommunicationManagerBuilder};
use rad_ble::{
    RadBleCandidateSummary, RadBleControlEvent, RadBleControlSetting, SharedRadBleState,
};

const UPSTREAM_BUTTPLUG_REVISION: &str = "9571b3db42ee2d7b3342ab9d40eb5c9e45679444";
const BUTTON_DEBOUNCE: Duration = Duration::from_millis(40);
const TEST_AUTO_APPROVE_NAME: Option<&str> = option_env!("RADR_BUTTPLUG_AUTO_APPROVE_NAME");

struct DebouncedButton {
    raw_pressed: bool,
    stable_pressed: bool,
    changed_at: Instant,
}

impl DebouncedButton {
    fn new(pressed: bool) -> Self {
        Self {
            raw_pressed: pressed,
            stable_pressed: pressed,
            changed_at: Instant::now(),
        }
    }

    fn update(&mut self, pressed: bool) -> bool {
        if pressed != self.raw_pressed {
            self.raw_pressed = pressed;
            self.changed_at = Instant::now();
        }
        if self.stable_pressed != self.raw_pressed && self.changed_at.elapsed() >= BUTTON_DEBOUNCE {
            self.stable_pressed = self.raw_pressed;
            return self.stable_pressed;
        }
        false
    }
}

const ENCODER_LOW_LIMIT: i32 = i16::MIN as i32;
const ENCODER_HIGH_LIMIT: i32 = i16::MAX as i32;
const ENCODER_COUNTS_PER_DETENT: i32 = 4;

struct QuadratureEncoder<'d> {
    unit: PcntUnitDriver<'d>,
    previous_detent: i32,
}

impl<'d> QuadratureEncoder<'d> {
    fn new(
        pin_a: impl InputPin + 'd,
        pin_b: impl InputPin + 'd,
    ) -> Result<Self, esp_idf_svc::sys::EspError> {
        let pin_a_number = pin_a.pin();
        let pin_b_number = pin_b.pin();
        esp_idf_svc::sys::EspError::convert(unsafe {
            esp_idf_svc::sys::gpio_set_pull_mode(
                pin_a_number.into(),
                esp_idf_svc::sys::gpio_pull_mode_t_GPIO_PULLUP_ONLY,
            )
        })?;
        esp_idf_svc::sys::EspError::convert(unsafe {
            esp_idf_svc::sys::gpio_set_pull_mode(
                pin_b_number.into(),
                esp_idf_svc::sys::gpio_pull_mode_t_GPIO_PULLUP_ONLY,
            )
        })?;

        let mut unit = PcntUnitDriver::new(&UnitConfig {
            low_limit: ENCODER_LOW_LIMIT,
            high_limit: ENCODER_HIGH_LIMIT,
            accum_count: true,
            ..Default::default()
        })?;
        unit.set_glitch_filter(Some(&GlitchFilterConfig {
            max_glitch: Duration::from_nanos(1_000),
            ..Default::default()
        }))?;

        let (duplicate_pin_a, duplicate_pin_b) = unsafe {
            (
                AnyInputPin::steal(pin_a_number),
                AnyInputPin::steal(pin_b_number),
            )
        };
        unit.add_channel(Some(pin_a), Some(pin_b), &Default::default())?
            .set_edge_action(ChannelEdgeAction::Decrease, ChannelEdgeAction::Increase)?
            .set_level_action(ChannelLevelAction::Keep, ChannelLevelAction::Inverse)?;
        unit.add_channel(
            Some(duplicate_pin_b),
            Some(duplicate_pin_a),
            &Default::default(),
        )?
        .set_edge_action(ChannelEdgeAction::Increase, ChannelEdgeAction::Decrease)?
        .set_level_action(ChannelLevelAction::Keep, ChannelLevelAction::Inverse)?;

        unit.enable()?;
        unit.add_watch_points_and_clear([ENCODER_LOW_LIMIT, ENCODER_HIGH_LIMIT])?;
        unit.start()?;

        Ok(Self {
            unit,
            previous_detent: 0,
        })
    }

    fn take_delta(&mut self) -> Result<i32, esp_idf_svc::sys::EspError> {
        let detent = self.unit.get_count()? / ENCODER_COUNTS_PER_DETENT;
        let delta = detent - self.previous_detent;
        self.previous_detent = detent;
        Ok(delta)
    }
}

#[derive(Clone)]
struct ActiveOutputControl {
    feature: ClientDeviceFeature,
    output_type: OutputType,
    label: String,
    value_percent: u8,
}

fn output_controls(device: &ButtplugClientDevice) -> Vec<ActiveOutputControl> {
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
            controls.push(ActiveOutputControl {
                feature: feature.clone(),
                output_type,
                label,
                value_percent: 0,
            });
        }
    }
    controls
}

fn publish_controls(state: &SharedRadBleState, controls: &[ActiveOutputControl], selected: usize) {
    rad_ble::with_state(state, |state| {
        state.selected_control_index = controls.get(selected).map(|_| selected);
        state.controls = controls
            .iter()
            .map(|control| RadBleControlSetting {
                feature_index: control.feature.feature_index(),
                label: control.label.clone(),
                output_type: control.output_type.to_string(),
                value_percent: control.value_percent,
            })
            .collect();
        state.control_revision = state.control_revision.wrapping_add(1);
    });
}

fn select_control(
    state: &SharedRadBleState,
    controls: &[ActiveOutputControl],
    selected: &mut usize,
    delta: i32,
) {
    if controls.is_empty() {
        return;
    }
    *selected = ((*selected as i32 + delta).rem_euclid(controls.len() as i32)) as usize;
    publish_controls(state, controls, *selected);
    log::info!(
        "Selected upstream control {} of {}: {}",
        *selected + 1,
        controls.len(),
        controls[*selected].label
    );
}

async fn adjust_control(
    state: &SharedRadBleState,
    controls: &mut [ActiveOutputControl],
    selected: usize,
    delta: i32,
) {
    let Some(control) = controls.get(selected) else {
        return;
    };
    let value_percent = (i32::from(control.value_percent) + delta).clamp(0, 100) as u8;
    if value_percent == control.value_percent {
        return;
    }
    let feature = control.feature.clone();
    let output_type = control.output_type;
    let output_name = output_type.to_string();
    let label = control.label.clone();
    let value = ClientDeviceCommandValue::Percent(f64::from(value_percent) / 100.0);
    let command = match output_type {
        OutputType::HwPositionWithDuration => {
            ClientDeviceOutputCommand::HwPositionWithDuration(value, 500)
        }
        output_type => match ClientDeviceOutputCommand::from_command_value(output_type, &value) {
            Ok(command) => command,
            Err(error) => {
                rad_ble::with_state(state, |state| state.last_error = Some(error.to_string()));
                return;
            }
        },
    };
    match feature.run_output(&command).await {
        Ok(()) => {
            if let Some(control) = controls.get_mut(selected) {
                control.value_percent = value_percent;
            }
            publish_controls(state, controls, selected);
            rad_ble::with_state(state, |state| state.last_error = None);
            log::info!(
                "BUTTPLUG_CONTROL feature={} output={} value={} label={:?}",
                feature.feature_index(),
                output_name,
                value_percent,
                label
            );
        }
        Err(error) => {
            log::warn!("Could not set upstream control {label:?}: {error}");
            rad_ble::with_state(state, |state| state.last_error = Some(error.to_string()));
        }
    }
}

async fn stop_active_device(
    state: &SharedRadBleState,
    device: Option<&ButtplugClientDevice>,
    controls: &mut [ActiveOutputControl],
    selected: usize,
) {
    let Some(device) = device else { return };
    match device.stop().await {
        Ok(()) => {
            for control in controls.iter_mut() {
                control.value_percent = 0;
            }
            publish_controls(state, controls, selected);
            rad_ble::with_state(state, |state| state.last_error = None);
            log::info!("User stopped all upstream outputs on {}", device.name());
        }
        Err(error) => {
            log::warn!(
                "Could not stop upstream outputs on {}: {error}",
                device.name()
            );
            rad_ble::with_state(state, |state| state.last_error = Some(error.to_string()));
        }
    }
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

fn publish_selection(state: &SharedRadBleState, candidates: &[Esp32BleCandidate], selected: usize) {
    rad_ble::with_state(state, |state| {
        state.candidate_count = candidates.len();
        state.candidates = candidates
            .iter()
            .map(|candidate| RadBleCandidateSummary {
                name: if candidate.name.is_empty() {
                    candidate.protocols.join(", ")
                } else {
                    candidate.name.clone()
                },
                protocols: candidate.protocols.clone(),
            })
            .collect();
        state.selected_index = candidates.get(selected).map(|_| selected);
        state.selected_name = candidates
            .get(selected)
            .map(|candidate| candidate.name.clone());
        state.selected_protocols = candidates
            .get(selected)
            .map(|candidate| candidate.protocols.clone())
            .unwrap_or_default();
        if !candidates.is_empty() && state.connected_name.is_none() {
            state.phase = "candidate".to_owned();
        }
    });
}

fn select_candidate(
    state: &SharedRadBleState,
    candidates: &[Esp32BleCandidate],
    selected: &mut usize,
    delta: i32,
) {
    if candidates.is_empty() {
        return;
    }
    *selected = ((*selected as i32 + delta).rem_euclid(candidates.len() as i32)) as usize;
    log_selected_candidate(candidates, *selected);
    publish_selection(state, candidates, *selected);
}

fn log_memory(stage: &str) {
    let (free, internal, external, minimum, largest, stack_headroom) = unsafe {
        (
            esp_idf_svc::sys::esp_get_free_heap_size(),
            esp_idf_svc::sys::esp_get_free_internal_heap_size(),
            esp_idf_svc::sys::heap_caps_get_free_size(esp_idf_svc::sys::MALLOC_CAP_SPIRAM),
            esp_idf_svc::sys::esp_get_minimum_free_heap_size(),
            esp_idf_svc::sys::heap_caps_get_largest_free_block(esp_idf_svc::sys::MALLOC_CAP_8BIT),
            esp_idf_svc::sys::uxTaskGetStackHighWaterMark(core::ptr::null_mut()),
        )
    };
    log::info!(
        "Memory at {stage}: free={free} internal={internal} external={external} minimum={minimum} largest_8bit_block={largest} main_stack_headroom={stack_headroom}"
    );
}

fn main() -> anyhow::Result<()> {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();
    unsafe {
        esp_idf_svc::sys::esp_log_level_set(
            c"NimBLE".as_ptr(),
            esp_idf_svc::sys::esp_log_level_t_ESP_LOG_WARN,
        );
    }

    log::info!("RADR upstream Buttplug probe starting");
    log::info!("Buttplug source revision: {UPSTREAM_BUTTPLUG_REVISION}");
    let parallelism = std::thread::available_parallelism().map_or(1, usize::from);
    let dashmap_default_shards = (parallelism * 4).next_power_of_two();
    log::info!(
        "Rust reports {parallelism} available processor(s); DashMap default would be {dashmap_default_shards} shards"
    );
    log_memory("startup");

    let rad_ble_state = rad_ble::shared_state();
    let peripherals = Peripherals::take().context("could not take the RADR GPIO peripherals")?;
    let pins = peripherals.pins;
    let mut display = radr_display::initialize(
        peripherals.spi2,
        pins.gpio5,
        pins.gpio6,
        pins.gpio4,
        pins.gpio7,
        pins.gpio16,
        pins.gpio15,
    );
    display
        .refresh(&rad_ble_state)
        .map_err(anyhow::Error::msg)?;
    let left_button = PinDriver::input(pins.gpio38, Pull::Up)
        .context("could not initialize the left under-screen button")?;
    let center_button = PinDriver::input(pins.gpio39, Pull::Up)
        .context("could not initialize the center under-screen button")?;
    let right_button = PinDriver::input(pins.gpio40, Pull::Up)
        .context("could not initialize the right under-screen button")?;
    let mut left_debounce = DebouncedButton::new(left_button.is_low());
    let mut center_debounce = DebouncedButton::new(center_button.is_low());
    let mut right_debounce = DebouncedButton::new(right_button.is_low());
    let mut left_encoder = QuadratureEncoder::new(pins.gpio10, pins.gpio11)
        .context("could not initialize the left hardware quadrature counter")?;
    let mut right_encoder = QuadratureEncoder::new(pins.gpio42, pins.gpio41)
        .context("could not initialize the right hardware quadrature counter")?;

    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_time()
        .build()
        .context("could not create the Buttplug Tokio runtime")?;

    runtime.block_on(async {
        let device_configuration = Arc::new(
            load_protocol_configs(&None, &None, false)
                .context("could not parse the upstream Buttplug device catalog")?
                .finish()
                .context("could not load the upstream Buttplug device catalog")?,
        );
        log_memory("upstream catalog loaded");

        let implemented_protocols: Arc<HashSet<String>> =
            Arc::new(get_default_protocol_map().into_keys().collect());
        let configured_ble_protocols: HashSet<&String> = device_configuration
            .base_communication_specifiers()
            .iter()
            .filter(|(_, specifiers)| {
                specifiers.iter().any(|specifier| {
                    matches!(specifier, ProtocolCommunicationSpecifier::BluetoothLE(_))
                })
            })
            .map(|(protocol, _)| protocol)
            .collect();
        let factory_backed_ble_protocol_count = configured_ble_protocols
            .iter()
            .filter(|protocol| implemented_protocols.contains(protocol.as_str()))
            .count();
        let mut configuration_only_protocols: Vec<&str> = configured_ble_protocols
            .iter()
            .filter(|protocol| !implemented_protocols.contains(protocol.as_str()))
            .map(|protocol| protocol.as_str())
            .collect();
        configuration_only_protocols.sort_unstable();
        log::info!(
            "Official protocol inventory: {} factories, {} BLE configs, {} factory-backed BLE protocol IDs",
            implemented_protocols.len(),
            configured_ble_protocols.len(),
            factory_backed_ble_protocol_count
        );
        if !configuration_only_protocols.is_empty() {
            log::warn!(
                "Ignoring upstream BLE config entries without protocol factories: {configuration_only_protocols:?}"
            );
        }

        // Candidate discovery must be lossless: a dense BLE environment may
        // surface many upstream-supported advertisements in one scan slice.
        let (candidate_sender, mut candidate_receiver) =
            tokio::sync::mpsc::unbounded_channel();
        let (rad_ble_controls, mut rad_ble_control_receiver) = tokio::sync::mpsc::channel(32);
        let (ble_manager_builder, candidate_approver) = Esp32BleCommunicationManagerBuilder::new(
            device_configuration.clone(),
            implemented_protocols,
            candidate_sender,
            rad_ble_controls,
            rad_ble_state.clone(),
        );
        let mut device_manager_builder =
            ServerDeviceManagerBuilder::new_with_arc(device_configuration.clone());
        device_manager_builder.comm_manager(ble_manager_builder);
        let device_manager = device_manager_builder
            .finish()
            .context("could not construct the Buttplug device manager")?;
        log_memory("ESP32 BLE manager created");

        let mut server_builder = ButtplugServerBuilder::new(device_manager);
        server_builder.name("RADR Buttplug");
        let server = server_builder
            .finish()
            .context("could not construct the Buttplug server")?;
        log_memory("upstream server created");

        let mut connector_builder = ButtplugInProcessClientConnectorBuilder::default();
        connector_builder.server(server);
        let connector = connector_builder.finish();
        let client = ButtplugClient::new("RADR");
        client
            .connect(connector)
            .await
            .context("could not connect RADR to its in-process Buttplug server")?;
        log_memory("in-process client connected");

        let mut events = client.event_stream();

        client
            .start_scanning()
            .await
            .context("could not start the upstream Buttplug scan")?;
        log::info!("Official Buttplug server, catalog, and ESP32 BLE manager initialized");
        log::info!(
            "Candidate approval: left/right under-screen buttons select; center connects"
        );
        if let Some(name) = TEST_AUTO_APPROVE_NAME {
            log::warn!("Test-only auto-approval is enabled for BLE name {name:?}");
        }

        let mut candidates = Vec::<Esp32BleCandidate>::new();
        let mut selected = 0_usize;
        let mut active_device: Option<ButtplugClientDevice> = None;
        let mut active_controls = Vec::<ActiveOutputControl>::new();
        let mut selected_control = 0_usize;
        let mut button_poll = tokio::time::interval(Duration::from_millis(10));
        button_poll.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        let mut encoder_poll = tokio::time::interval(Duration::from_millis(10));
        encoder_poll.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        let mut display_poll = tokio::time::interval(Duration::from_millis(100));
        display_poll.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

        loop {
            tokio::select! {
                event = events.next() => {
                    let Some(event) = event else {
                        anyhow::bail!("Official Buttplug client event stream ended unexpectedly");
                    };
                    match event {
                        ButtplugClientEvent::DeviceAdded(device) => {
                            log::info!("Buttplug device connected: {}", device.name());
                            let mut features = Vec::new();
                            for (index, feature) in device.device_features() {
                                log::info!(
                                    "Buttplug feature {index} on {}: {:?}",
                                    device.name(),
                                    feature.feature()
                                );
                                let feature_json = serde_json::json!({
                                    "device": device.name(),
                                    "feature_index": index,
                                    "definition": feature.feature(),
                                });
                                log::info!("BUTTPLUG_FEATURE_JSON {feature_json}");
                                features.push(feature_json);
                            }
                            active_controls = output_controls(&device);
                            selected_control = 0;
                            rad_ble::with_state(&rad_ble_state, |state| {
                                state.phase = "connected".to_owned();
                                state.connected_name = Some(device.name().to_owned());
                                state.feature_count = features.len();
                                state.features = features.clone();
                                state.last_connected_name = Some(device.name().to_owned());
                                state.last_feature_count = features.len();
                                state.last_features = features;
                                state.last_error = None;
                            });
                            publish_controls(
                                &rad_ble_state,
                                &active_controls,
                                selected_control,
                            );
                            active_device = Some(device.clone());
                            match device.stop().await {
                                Ok(()) => log::info!(
                                    "Sent upstream all-stop command to {} after enumeration",
                                    device.name()
                                ),
                                Err(error) => log::warn!(
                                    "Could not send upstream all-stop command to {}: {error}",
                                    device.name()
                                ),
                            }
                            log_memory("device connected, enumerated, and stopped");
                        }
                        ButtplugClientEvent::DeviceRemoved(device) => {
                            log::info!("Buttplug device disconnected: {}", device.name());
                            let removed_active = active_device
                                .as_ref()
                                .is_some_and(|active| active.index() == device.index());
                            if removed_active {
                                active_device = None;
                                active_controls.clear();
                                selected_control = 0;
                                rad_ble::with_state(&rad_ble_state, |state| {
                                    state.phase = if candidates.is_empty() {
                                        "scanning".to_owned()
                                    } else {
                                        "candidate".to_owned()
                                    };
                                    state.connected_name = None;
                                    state.feature_count = 0;
                                    state.features.clear();
                                    state.selected_control_index = None;
                                    state.controls.clear();
                                    state.control_revision = state.control_revision.wrapping_add(1);
                                });
                                publish_selection(&rad_ble_state, &candidates, selected);
                            }
                        }
                        ButtplugClientEvent::ScanningFinished => {
                            log::info!("Buttplug BLE scan finished");
                            rad_ble::with_state(&rad_ble_state, |state| {
                                if state.connected_name.is_none() {
                                    state.phase = "scan_finished".to_owned();
                                }
                            });
                        }
                        ButtplugClientEvent::Error(error) => {
                            log::error!("Buttplug client error: {error}");
                            rad_ble::with_state(&rad_ble_state, |state| {
                                state.phase = "error".to_owned();
                                state.last_error = Some(error.to_string());
                            });
                        }
                        _ => {}
                    }
                }
                candidate = candidate_receiver.recv() => {
                    let Some(candidate) = candidate else {
                        anyhow::bail!("ESP32 BLE candidate channel closed unexpectedly");
                    };
                    let candidate_index = if let Some(index) = candidates
                        .iter()
                        .position(|known| known.address == candidate.address)
                    {
                        candidates[index] = candidate.clone();
                        index
                    } else {
                        candidates.push(candidate.clone());
                        candidates.len() - 1
                    };
                    log::info!(
                        "Supported BLE candidate {}: name={:?} address={} protocols={:?} services={:?} manufacturer_ids={:?}",
                        candidate_index + 1,
                        candidate.name,
                        candidate.address,
                        candidate.protocols,
                        candidate.advertised_services,
                        candidate.manufacturer_ids
                    );
                    if candidates.len() == 1 {
                        selected = 0;
                        log_selected_candidate(&candidates, selected);
                    }
                    publish_selection(&rad_ble_state, &candidates, selected);
                    if TEST_AUTO_APPROVE_NAME == Some(candidate.name.as_str()) {
                        log::warn!(
                            "Test-only auto-approving {:?} at {}",
                            candidate.name,
                            candidate.address
                        );
                        candidate_approver
                            .approve(&candidate.address)
                            .map_err(anyhow::Error::msg)?;
                        rad_ble::with_state(&rad_ble_state, |state| {
                            state.phase = "connecting".to_owned();
                        });
                    }
                }
                control = rad_ble_control_receiver.recv() => {
                    let Some(control) = control else {
                        anyhow::bail!("Rust RAD BLE control channel closed unexpectedly");
                    };
                    log::info!("RAD BLE injected control: {control:?}");
                    match control {
                        RadBleControlEvent::Left => {
                            if active_device.is_some() {
                                select_control(
                                    &rad_ble_state,
                                    &active_controls,
                                    &mut selected_control,
                                    -1,
                                );
                            } else {
                                select_candidate(
                                    &rad_ble_state,
                                    &candidates,
                                    &mut selected,
                                    -1,
                                );
                            }
                        }
                        RadBleControlEvent::Right => {
                            if active_device.is_some() {
                                select_control(
                                    &rad_ble_state,
                                    &active_controls,
                                    &mut selected_control,
                                    1,
                                );
                            } else {
                                select_candidate(
                                    &rad_ble_state,
                                    &candidates,
                                    &mut selected,
                                    1,
                                );
                            }
                        }
                        RadBleControlEvent::Middle { defer_ms } => {
                            if defer_ms > 0 {
                                log::info!(
                                    "Deferring RAD BLE action by {defer_ms} ms so the control connection can close"
                                );
                                tokio::time::sleep(Duration::from_millis(u64::from(defer_ms))).await;
                            }
                            if active_device.is_some() {
                                stop_active_device(
                                    &rad_ble_state,
                                    active_device.as_ref(),
                                    &mut active_controls,
                                    selected_control,
                                )
                                .await;
                            } else if let Some(candidate) = candidates.get(selected) {
                                log::info!(
                                    "RAD BLE approved {:?} at {} for connection",
                                    candidate.name,
                                    candidate.address
                                );
                                candidate_approver
                                    .approve(&candidate.address)
                                    .map_err(anyhow::Error::msg)?;
                                rad_ble::with_state(&rad_ble_state, |state| {
                                    state.phase = "connecting".to_owned();
                                });
                            } else {
                                log::info!("No supported BLE candidate is available to approve");
                            }
                        }
                        RadBleControlEvent::EncoderLeft { delta } => {
                            if active_device.is_some() {
                                adjust_control(
                                    &rad_ble_state,
                                    &mut active_controls,
                                    selected_control,
                                    delta,
                                )
                                .await;
                            } else {
                                select_candidate(
                                    &rad_ble_state,
                                    &candidates,
                                    &mut selected,
                                    delta.signum(),
                                );
                            }
                        }
                        RadBleControlEvent::EncoderRight { delta } => {
                            if active_device.is_some() {
                                select_control(
                                    &rad_ble_state,
                                    &active_controls,
                                    &mut selected_control,
                                    delta.signum(),
                                );
                            } else {
                                select_candidate(
                                    &rad_ble_state,
                                    &candidates,
                                    &mut selected,
                                    delta.signum(),
                                );
                            }
                        }
                        RadBleControlEvent::Reset => {
                            log::info!("RAD BLE requested a fresh upstream scan");
                            if let Err(error) = client.stop_scanning().await {
                                log::warn!("Could not stop scan during test reset: {error}");
                            }
                            stop_active_device(
                                &rad_ble_state,
                                active_device.as_ref(),
                                &mut active_controls,
                                selected_control,
                            )
                            .await;
                            active_device = None;
                            active_controls.clear();
                            selected_control = 0;
                            candidates.clear();
                            selected = 0;
                            rad_ble::with_state(&rad_ble_state, |state| {
                                state.phase = "scanning".to_owned();
                                state.candidate_count = 0;
                                state.selected_index = None;
                                state.selected_name = None;
                                state.selected_protocols.clear();
                                state.candidates.clear();
                                state.connected_name = None;
                                state.feature_count = 0;
                                state.features.clear();
                                state.selected_control_index = None;
                                state.controls.clear();
                                state.control_revision = state.control_revision.wrapping_add(1);
                                state.last_connected_name = None;
                                state.last_feature_count = 0;
                                state.last_features.clear();
                                state.last_error = None;
                            });
                            client
                                .start_scanning()
                                .await
                                .context("could not restart the upstream Buttplug scan")?;
                        }
                    }
                }
                _ = button_poll.tick() => {
                    if left_debounce.update(left_button.is_low()) {
                        if active_device.is_some() {
                            select_control(
                                &rad_ble_state,
                                &active_controls,
                                &mut selected_control,
                                -1,
                            );
                        } else {
                            select_candidate(
                                &rad_ble_state,
                                &candidates,
                                &mut selected,
                                -1,
                            );
                        }
                    }
                    if right_debounce.update(right_button.is_low()) {
                        if active_device.is_some() {
                            select_control(
                                &rad_ble_state,
                                &active_controls,
                                &mut selected_control,
                                1,
                            );
                        } else {
                            select_candidate(
                                &rad_ble_state,
                                &candidates,
                                &mut selected,
                                1,
                            );
                        }
                    }
                    if center_debounce.update(center_button.is_low()) {
                        if active_device.is_some() {
                            stop_active_device(
                                &rad_ble_state,
                                active_device.as_ref(),
                                &mut active_controls,
                                selected_control,
                            )
                            .await;
                        } else if let Some(candidate) = candidates.get(selected) {
                            log::info!(
                                "User approved {:?} at {} for connection",
                                candidate.name,
                                candidate.address
                            );
                            candidate_approver
                                .approve(&candidate.address)
                                .map_err(anyhow::Error::msg)?;
                            rad_ble::with_state(&rad_ble_state, |state| {
                                state.phase = "connecting".to_owned();
                            });
                        } else {
                            log::info!("No supported BLE candidate is available to approve");
                        }
                    }
                }
                _ = encoder_poll.tick() => {
                    let left_delta = left_encoder
                        .take_delta()
                        .context("could not read the left hardware quadrature counter")?;
                    if left_delta != 0 {
                        log::info!("Physical left encoder delta: {left_delta}");
                        if active_device.is_some() {
                            adjust_control(
                                &rad_ble_state,
                                &mut active_controls,
                                selected_control,
                                left_delta * 5,
                            )
                            .await;
                        } else {
                            select_candidate(
                                &rad_ble_state,
                                &candidates,
                                &mut selected,
                                left_delta,
                            );
                        }
                    }

                    let right_delta = right_encoder
                        .take_delta()
                        .context("could not read the right hardware quadrature counter")?;
                    if right_delta != 0 {
                        log::info!("Physical right encoder delta: {right_delta}");
                        if active_device.is_some() {
                            select_control(
                                &rad_ble_state,
                                &active_controls,
                                &mut selected_control,
                                right_delta,
                            );
                        } else {
                            select_candidate(
                                &rad_ble_state,
                                &candidates,
                                &mut selected,
                                right_delta,
                            );
                        }
                    }
                }
                _ = display_poll.tick() => {
                    if let Err(error) = display.refresh(&rad_ble_state) {
                        log::error!("RADR display refresh failed: {error}");
                        rad_ble::with_state(&rad_ble_state, |state| {
                            state.last_error = Some(error);
                        });
                    }
                }
            }
        }
    })
}
