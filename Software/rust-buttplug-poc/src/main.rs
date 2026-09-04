mod ble_policy;
use anyhow::Context;
use buttplug_client::ButtplugClient;
use buttplug_client_in_process::ButtplugInProcessClientConnectorBuilder;
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

mod controller;
mod controls;
mod device_names;
mod esp32_ble;
mod flow;
mod psram_allocator;
mod rad_ble;
mod radr_display;
mod runtime_state;
mod screen;
mod ui;

use esp32_ble::Esp32BleCommunicationManagerBuilder;
use rad_ble::RadBleControlEvent;

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

use controls::{
    ActiveOutputControl, output_controls, publish_controls, publish_selection, select_candidate,
    select_control,
};

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

        let mut controller = controller::Controller::new(Arc::new(client), candidate_approver, rad_ble_state.clone());
        let mut input_poll = tokio::time::interval(Duration::from_millis(10));
        input_poll.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        let mut display_poll = tokio::time::interval(Duration::from_millis(100));
        display_poll.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

        loop {
            tokio::select! {
                event = events.next() => {
                    let Some(event) = event else { anyhow::bail!("Buttplug event stream ended"); };
                    controller.client_event(event);
                }
                snapshot = candidate_receiver.recv() => {
                    let Some(snapshot) = snapshot else { anyhow::bail!("BLE discovery channel closed"); };
                    controller.discovery(snapshot);
                }
                control = rad_ble_control_receiver.recv() => {
                    if let Some(control) = control { controller.input(control); }
                }
                _ = input_poll.tick() => {
                    if left_debounce.update(left_button.is_low()) { controller.input(RadBleControlEvent::Left); }
                    if center_debounce.update(center_button.is_low()) { controller.input(RadBleControlEvent::Middle { defer_ms: 0 }); }
                    if right_debounce.update(right_button.is_low()) { controller.input(RadBleControlEvent::Right); }
                    let left_delta = left_encoder.take_delta()?;
                    if left_delta != 0 { controller.physical_left(left_delta); }
                    let right_delta = right_encoder.take_delta()?;
                    if right_delta != 0 { controller.input(RadBleControlEvent::EncoderRight { delta: right_delta }); }
                    controller.tick();
                }
                _ = display_poll.tick() => {
                    if let Err(error) = display.refresh(&rad_ble_state) { log::error!("RADR display refresh failed: {error}"); }
                }
            }
        }
    })
}
