use anyhow::Context;
use buttplug_client::{ButtplugClient, ButtplugClientEvent};
use buttplug_client_in_process::ButtplugInProcessClientConnectorBuilder;
use buttplug_server::{ButtplugServerBuilder, device::ServerDeviceManagerBuilder};
use buttplug_server_device_config::load_protocol_configs;
use esp_idf_svc::hal::{
    gpio::{PinDriver, Pull},
    peripherals::Peripherals,
};
use futures::StreamExt;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

mod esp32_ble;
mod psram_allocator;

use esp32_ble::{Esp32BleCandidate, Esp32BleCommunicationManagerBuilder};

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

    let peripherals = Peripherals::take().context("could not take the RADR GPIO peripherals")?;
    let pins = peripherals.pins;
    let left_button = PinDriver::input(pins.gpio38, Pull::Up)
        .context("could not initialize the left under-screen button")?;
    let center_button = PinDriver::input(pins.gpio39, Pull::Up)
        .context("could not initialize the center under-screen button")?;
    let right_button = PinDriver::input(pins.gpio40, Pull::Up)
        .context("could not initialize the right under-screen button")?;
    let mut left_debounce = DebouncedButton::new(left_button.is_low());
    let mut center_debounce = DebouncedButton::new(center_button.is_low());
    let mut right_debounce = DebouncedButton::new(right_button.is_low());

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

        let (candidate_sender, mut candidate_receiver) = tokio::sync::mpsc::channel(32);
        let (ble_manager_builder, candidate_approver) =
            Esp32BleCommunicationManagerBuilder::new(device_configuration.clone(), candidate_sender);
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
        tokio::spawn(async move {
            while let Some(event) = events.next().await {
                match event {
                    ButtplugClientEvent::DeviceAdded(device) => {
                        log::info!("Buttplug device connected: {}", device.name());
                        for (index, feature) in device.device_features() {
                            log::info!(
                                "Buttplug feature {index} on {}: {:?}",
                                device.name(),
                                feature.feature()
                            );
                        }
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
                        log_memory("device connected and stopped");
                    }
                    ButtplugClientEvent::DeviceRemoved(device) => {
                        log::info!("Buttplug device disconnected: {}", device.name());
                    }
                    ButtplugClientEvent::ScanningFinished => {
                        log::info!("Buttplug BLE scan finished");
                    }
                    ButtplugClientEvent::Error(error) => {
                        log::error!("Buttplug client error: {error}");
                    }
                    _ => {}
                }
            }
        });

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
        let mut button_poll = tokio::time::interval(Duration::from_millis(10));
        button_poll.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

        loop {
            tokio::select! {
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
                    if TEST_AUTO_APPROVE_NAME == Some(candidate.name.as_str()) {
                        log::warn!(
                            "Test-only auto-approving {:?} at {}",
                            candidate.name,
                            candidate.address
                        );
                        candidate_approver
                            .approve(&candidate.address)
                            .map_err(anyhow::Error::msg)?;
                    }
                }
                _ = button_poll.tick() => {
                    if left_debounce.update(left_button.is_low()) && !candidates.is_empty() {
                        selected = (selected + candidates.len() - 1) % candidates.len();
                        log_selected_candidate(&candidates, selected);
                    }
                    if right_debounce.update(right_button.is_low()) && !candidates.is_empty() {
                        selected = (selected + 1) % candidates.len();
                        log_selected_candidate(&candidates, selected);
                    }
                    if center_debounce.update(center_button.is_low()) {
                        if let Some(candidate) = candidates.get(selected) {
                            log::info!(
                                "User approved {:?} at {} for connection",
                                candidate.name,
                                candidate.address
                            );
                            candidate_approver
                                .approve(&candidate.address)
                                .map_err(anyhow::Error::msg)?;
                        } else {
                            log::info!("No supported BLE candidate is available to approve");
                        }
                    }
                }
            }
        }
    })
}
