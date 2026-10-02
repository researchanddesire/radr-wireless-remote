//! One owner for physical/RAD BLE input, lifecycle transitions, and asynchronous results.
use crate::{
    ActiveOutputControl, TEST_AUTO_APPROVE_NAME,
    esp32_ble::{DiscoverySnapshot, Esp32BleCandidate, Esp32BleCandidateApprover},
    flow::{CommandSequence, Flow, Phase},
    output_controls, publish_controls, publish_selection,
    rad_ble::{self, RadBleControlEvent, SharedRadBleState},
    select_candidate, select_control,
};
use buttplug_client::{ButtplugClient, ButtplugClientDevice, ButtplugClientEvent};

use std::{
    collections::HashMap,
    sync::Arc,
    time::{Duration, Instant},
};
use tokio::sync::mpsc;

const COMMAND_TIMEOUT: Duration = Duration::from_secs(5);

enum Completion {
    Scan(Result<(), String>),
    Output {
        sequence: u64,
        generation: u32,
        index: usize,
        value: i16,
        result: Result<(), String>,
    },
    Stop {
        sequence: u64,
        generation: u32,
        initial: bool,
        result: Result<(), String>,
    },
    Disconnected {
        destination: Phase,
        result: Result<(), String>,
    },
}

pub struct Controller {
    client: Arc<ButtplugClient>,
    approver: Esp32BleCandidateApprover,
    state: SharedRadBleState,
    flow: Flow,
    candidates: Vec<Esp32BleCandidate>,
    selected: usize,
    target: Option<Esp32BleCandidate>,
    device: Option<ButtplugClientDevice>,
    controls: Vec<ActiveOutputControl>,
    desired_values: Vec<i16>,
    known_names: HashMap<String, String>,
    stopping: bool,
    selected_control: usize,
    generation: u32,
    command_pending: bool,
    commands: CommandSequence,
    scan_pending: bool,
    retry_at: Option<Instant>,
    deferred: Option<(Instant, RadBleControlEvent)>,
    sender: mpsc::UnboundedSender<Completion>,
    receiver: mpsc::UnboundedReceiver<Completion>,
}

impl Controller {
    pub fn new(
        client: Arc<ButtplugClient>,
        approver: Esp32BleCandidateApprover,
        state: SharedRadBleState,
    ) -> Self {
        let (sender, receiver) = mpsc::unbounded_channel();
        let mut controller = Self {
            client,
            approver,
            state,
            flow: Flow::new(Instant::now()),
            candidates: Vec::new(),
            selected: 0,
            target: None,
            device: None,
            controls: Vec::new(),
            desired_values: Vec::new(),
            known_names: HashMap::new(),
            stopping: false,
            selected_control: 0,
            generation: 0,
            command_pending: false,
            commands: CommandSequence::default(),
            scan_pending: false,
            retry_at: None,
            deferred: None,
            sender,
            receiver,
        };
        controller.enter(Phase::Searching, "Looking for devices");
        controller
    }

    fn enter(&mut self, phase: Phase, status: &str) {
        self.flow.enter(phase, Instant::now());
        rad_ble::with_state(&self.state, |state| {
            state.phase = phase.as_str().to_owned();
            state.status = status.to_owned();
            state.elapsed_seconds = 0;
        });
    }

    fn error(&mut self, code: &str, detail: String) {
        log::warn!("{code}: {detail}");
        rad_ble::with_state(&self.state, |state| {
            state.error_code = Some(code.to_owned());
            state.last_error = Some(detail);
        });
    }

    fn clear_error(&self) {
        rad_ble::with_state(&self.state, |state| {
            state.error_code = None;
            state.last_error = None;
        });
    }

    pub fn discovery(&mut self, snapshot: DiscoverySnapshot) {
        if self.flow.phase != Phase::Searching || self.flow.browsing(Instant::now()) {
            return;
        }
        let selected_address = self
            .candidates
            .get(self.selected)
            .map(|candidate| candidate.address.clone());
        if snapshot.complete {
            self.candidates.retain(|known| {
                snapshot
                    .candidates
                    .iter()
                    .any(|candidate| candidate.address == known.address)
            });
        }
        for mut candidate in snapshot.candidates {
            if let Some(name) = self.known_names.get(&candidate.address) {
                candidate.display_name = name.clone();
            }
            if let Some(known) = self
                .candidates
                .iter_mut()
                .find(|known| known.address == candidate.address)
            {
                *known = candidate;
            } else {
                self.candidates.push(candidate);
            }
        }
        if snapshot.complete {
            self.known_names.retain(|address, _| {
                self.candidates
                    .iter()
                    .any(|candidate| &candidate.address == address)
            });
        }
        self.selected = selected_address
            .and_then(|address| {
                self.candidates
                    .iter()
                    .position(|candidate| candidate.address == address)
            })
            .unwrap_or(self.selected.min(self.candidates.len().saturating_sub(1)));
        publish_selection(&self.state, &self.candidates, self.selected);
        if let Some(index) = self
            .candidates
            .iter()
            .position(|candidate| TEST_AUTO_APPROVE_NAME == Some(candidate.name.as_str()))
        {
            self.selected = index;
            self.connect_selected();
        }
    }

    pub fn physical_left(&mut self, delta: i32) {
        let event = if self.flow.phase == Phase::Connected {
            RadBleControlEvent::EncoderLeft {
                delta: delta.saturating_mul(5),
            }
        } else {
            RadBleControlEvent::EncoderRight { delta }
        };
        self.input(event);
    }

    pub fn input(&mut self, event: RadBleControlEvent) {
        let now = Instant::now();
        self.flow.interact(now);
        if let RadBleControlEvent::Middle { defer_ms } = event
            && defer_ms > 0
        {
            // Keep the UI and physical controls alive while automation closes its link.
            self.deferred = Some((
                now + Duration::from_millis(u64::from(defer_ms.min(5000))),
                RadBleControlEvent::Middle { defer_ms: 0 },
            ));
            return;
        }
        self.deferred = None;
        if event == RadBleControlEvent::Reset {
            if self.flow.phase != Phase::Disconnecting {
                self.disconnect(Phase::Searching);
            }
            return;
        }
        match self.flow.phase {
            Phase::Searching => {
                self.approver.pause_scan();
                match event {
                    RadBleControlEvent::Left => self.enter(Phase::Menu, "Find a device"),
                    RadBleControlEvent::Middle { .. } => self.restart_search(),
                    RadBleControlEvent::Right => self.connect_selected(),
                    RadBleControlEvent::EncoderLeft { delta } => select_candidate(
                        &self.state,
                        &self.candidates,
                        &mut self.selected,
                        delta.signum(),
                    ),
                    RadBleControlEvent::EncoderRight { delta } => {
                        select_candidate(&self.state, &self.candidates, &mut self.selected, delta)
                    }
                    _ => {}
                }
            }
            Phase::Menu => {
                if matches!(
                    event,
                    RadBleControlEvent::Right | RadBleControlEvent::Middle { .. }
                ) {
                    self.restart_search();
                }
            }
            Phase::Connecting => {
                if event == RadBleControlEvent::Left {
                    self.disconnect(Phase::Searching);
                }
            }
            Phase::Connected => match event {
                RadBleControlEvent::Left => self.disconnect(Phase::Searching),
                RadBleControlEvent::Middle { .. } => self.stop(false),
                RadBleControlEvent::Right => {
                    select_control(&self.state, &self.controls, &mut self.selected_control, 1)
                }
                RadBleControlEvent::EncoderRight { delta } => select_control(
                    &self.state,
                    &self.controls,
                    &mut self.selected_control,
                    delta,
                ),
                RadBleControlEvent::EncoderLeft { delta } => self.adjust(delta),
                _ => {}
            },
            Phase::Reconnecting => {
                if event == RadBleControlEvent::Left {
                    self.disconnect(Phase::Searching);
                }
            }
            Phase::Error => match event {
                RadBleControlEvent::Left => self.disconnect(Phase::Searching),
                RadBleControlEvent::Right | RadBleControlEvent::Middle { .. } => {
                    self.disconnect(Phase::Searching)
                }
                _ => {}
            },
            Phase::Disconnecting => {}
        }
    }

    fn restart_search(&mut self) {
        self.clear_error();
        self.candidates.clear();
        self.selected = 0;
        publish_selection(&self.state, &self.candidates, self.selected);
        self.enter(Phase::Searching, "Looking for devices");
        // An explicit refresh bypasses the interaction hold.
        self.flow.resume_search(Instant::now());
        self.start_scan();
    }

    fn start_scan(&mut self) {
        if self.scan_pending || self.flow.phase != Phase::Searching {
            return;
        }
        self.approver.allow_scan();
        self.scan_pending = true;
        self.flow.searched(Instant::now());
        let client = self.client.clone();
        let sender = self.sender.clone();
        tokio::spawn(async move {
            // Clear the server's scanning flag before each bounded discovery burst.
            let result = async {
                client
                    .stop_scanning()
                    .await
                    .map_err(|error| error.to_string())?;
                client
                    .start_scanning()
                    .await
                    .map_err(|error| error.to_string())
            }
            .await;
            let _ = sender.send(Completion::Scan(result));
        });
    }

    fn connect_selected(&mut self) {
        let Some(target) = self.candidates.get(self.selected).cloned() else {
            return;
        };
        self.target = Some(target);
        self.flow.begin_connection(Instant::now());
        self.enter(Phase::Connecting, "Getting ready");
        publish_selection(&self.state, &self.candidates, self.selected);
        self.approve_target();
    }

    fn approve_target(&mut self) {
        self.clear_error();
        self.command_pending = false;
        self.stopping = false;
        let Some(target) = &self.target else {
            return;
        };
        match self.approver.approve(&target.address) {
            Ok(generation) => {
                self.generation = generation;
                rad_ble::with_state(&self.state, |state| {
                    state.connection_generation = generation;
                });
            }
            Err(error) => {
                self.error("E202", error);
                self.enter(Phase::Error, "Could not start connection");
            }
        }
    }

    pub fn client_event(&mut self, event: ButtplugClientEvent) {
        match event {
            ButtplugClientEvent::DeviceAdded(device) => {
                if !self.flow.phase.waiting()
                    || self.device.is_some()
                    || self.retry_at.is_some()
                    || !device.connected()
                    || !self.approver.has_live_connection(self.generation)
                {
                    log::warn!("Ignoring a late device-added event");
                    return;
                }
                self.approver.pause_scan();
                let features: Vec<_> = device.device_features().iter().map(|(index, feature)| {
                    let value = serde_json::json!({"device": device.name(), "feature_index": index, "definition": feature.feature()});
                    log::info!("BUTTPLUG_FEATURE_JSON {value}"); value
                }).collect();
                self.controls = output_controls(&device);
                self.desired_values = vec![0; self.controls.len()];
                self.selected_control = 0;
                rad_ble::with_state(&self.state, |state| {
                    state.connected_name = Some(device.name().to_owned());
                    state.status = "Setting outputs to zero".to_owned();
                    state.feature_count = features.len();
                    state.features = features.clone();
                    state.last_connected_name = Some(device.name().to_owned());
                    state.last_feature_count = features.len();
                    state.last_features = features;
                });
                if let Some(target) = &mut self.target {
                    target.display_name = device.name().to_owned();
                    self.known_names
                        .insert(target.address.clone(), target.display_name.clone());
                    if let Some(candidate) = self
                        .candidates
                        .iter_mut()
                        .find(|candidate| candidate.address == target.address)
                    {
                        candidate.display_name = target.display_name.clone();
                    }
                }
                self.device = Some(device);
                publish_controls(&self.state, &self.controls, self.selected_control);
                self.stop(true);
            }
            ButtplugClientEvent::DeviceRemoved(device) => {
                if self
                    .device
                    .as_ref()
                    .is_some_and(|active| active.index() == device.index())
                {
                    self.device = None;
                    self.command_pending = false;
                    if self.flow.connection_lost(Instant::now()) {
                        // Keep the controls on screen, but never replay their old values.
                        self.enter(Phase::Reconnecting, "Connection lost. Trying again");
                        self.retry_at = Some(Instant::now() + Duration::from_secs(3));
                    } else if self.flow.phase.waiting() {
                        self.connection_failed(
                            "E204",
                            "Device disconnected during setup".to_owned(),
                        );
                    }
                }
            }
            ButtplugClientEvent::Error(error) => {
                self.error("E205", error.to_string());
                if self.flow.phase.waiting() {
                    self.connection_failed("E205", error.to_string());
                }
            }
            ButtplugClientEvent::ScanningFinished => {}
            _ => {}
        }
    }

    fn stop(&mut self, initial: bool) {
        if self.stopping {
            return;
        }
        self.stopping = true;
        self.desired_values.fill(0);
        let Some(device) = self.device.clone() else {
            return;
        };
        // A stop is allowed to overtake a pending value change at the application layer.
        self.command_pending = true;
        let sequence = self.commands.advance();
        let generation = self.generation;
        let sender = self.sender.clone();
        tokio::spawn(async move {
            let result = bounded(device.stop()).await;
            let _ = sender.send(Completion::Stop {
                sequence,
                generation,
                initial,
                result,
            });
        });
    }

    fn adjust(&mut self, delta: i32) {
        if self.stopping {
            return;
        }
        let Some(value) = self.desired_values.get_mut(self.selected_control) else {
            return;
        };
        let minimum = self.controls[self.selected_control].minimum_percent;
        *value = i32::from(*value)
            .saturating_add(delta)
            .clamp(i32::from(minimum), 100) as i16;
        self.dispatch_output();
    }

    fn dispatch_output(&mut self) {
        if self.command_pending || self.flow.phase != Phase::Connected {
            return;
        }
        let Some(index) = self
            .controls
            .iter()
            .enumerate()
            .find_map(|(index, control)| {
                (self.desired_values.get(index).copied().unwrap_or(0) != control.value_percent)
                    .then_some(index)
            })
        else {
            return;
        };
        let control = &self.controls[index];
        let value = self.desired_values[index];
        let command = control.command(value);
        let command = match command {
            Ok(command) => command,
            Err(error) => {
                self.error("E301", error.to_string());
                return;
            }
        };
        let feature = control.feature.clone();
        let generation = self.generation;
        let sender = self.sender.clone();
        self.command_pending = true;
        let sequence = self.commands.advance();
        tokio::spawn(async move {
            let result = bounded(feature.run_output(&command)).await;
            let _ = sender.send(Completion::Output {
                sequence,
                generation,
                index,
                value,
                result,
            });
        });
    }

    fn disconnect(&mut self, destination: Phase) {
        self.retry_at = None;
        self.deferred = None;
        self.commands.advance();
        self.enter(Phase::Disconnecting, "Stopping and disconnecting");
        self.approver.pause_scan();
        let device = self.device.take();
        let approver = self.approver.clone();
        let sender = self.sender.clone();
        tokio::spawn(async move {
            let stop_result = if let Some(device) = device {
                bounded(device.stop()).await
            } else {
                Ok(())
            };
            let disconnect_result = approver
                .cancel()
                .await
                .unwrap_or_else(|_| Err("BLE worker unavailable".to_owned()));
            let _ = sender.send(Completion::Disconnected {
                destination,
                result: stop_result.and(disconnect_result),
            });
        });
    }

    fn connection_failed(&mut self, code: &str, detail: String) {
        self.error(code, detail);
        self.commands.advance();
        drop(self.approver.cancel());
        self.device = None;
        self.command_pending = false;
        if self.flow.phase == Phase::Reconnecting
            && self.flow.attempt < crate::flow::MAX_RECONNECT_ATTEMPTS
        {
            self.retry_at = Some(Instant::now() + Duration::from_secs(3));
            rad_ble::with_state(&self.state, |state| {
                state.status = "Waiting to reconnect".to_owned();
            });
        } else {
            self.enter(Phase::Error, "Could not connect. Check device power");
        }
    }

    pub fn tick(&mut self) {
        while let Ok(completion) = self.receiver.try_recv() {
            match completion {
                Completion::Scan(result) => {
                    self.scan_pending = false;
                    if self.flow.phase != Phase::Searching {
                        self.approver.pause_scan();
                    }
                    if let Err(error) = result {
                        self.error("E101", error);
                    }
                }
                Completion::Output {
                    sequence,
                    generation,
                    index,
                    value,
                    result,
                } => {
                    if !self.commands.accepts(sequence)
                        || generation != self.generation
                        || self.flow.phase != Phase::Connected
                    {
                        continue;
                    }
                    self.command_pending = false;
                    match result {
                        Ok(()) => {
                            if let Some(control) = self.controls.get_mut(index) {
                                control.value_percent = value;
                            }
                            publish_controls(&self.state, &self.controls, self.selected_control);
                            self.clear_error();
                        }
                        Err(error) => {
                            if self.flow.connection_lost(Instant::now()) {
                                self.enter(Phase::Reconnecting, "Command failed. Reconnecting");
                            }
                            self.connection_failed("E301", error);
                        }
                    }
                }
                Completion::Stop {
                    sequence,
                    generation,
                    initial,
                    result,
                } => {
                    if !self.commands.accepts(sequence)
                        || generation != self.generation
                        || !(self.flow.phase.waiting() || self.flow.phase == Phase::Connected)
                    {
                        continue;
                    }
                    self.command_pending = false;
                    self.stopping = false;
                    match result {
                        Ok(()) => {
                            for control in &mut self.controls {
                                control.value_percent = 0;
                            }
                            publish_controls(&self.state, &self.controls, self.selected_control);
                            self.clear_error();
                            if initial
                                && self.device.is_some()
                                && self.flow.connection_ready(Instant::now())
                            {
                                self.retry_at = None;
                                self.enter(Phase::Connected, "Connected");
                            }
                        }
                        Err(error) => {
                            self.connection_failed("E302", error);
                        }
                    }
                }
                Completion::Disconnected {
                    destination,
                    result,
                } => {
                    self.controls.clear();
                    self.desired_values.clear();
                    self.stopping = false;
                    self.command_pending = false;
                    self.target = None;
                    rad_ble::with_state(&self.state, |state| {
                        state.connected_name = None;
                        state.controls.clear();
                        state.features.clear();
                        state.feature_count = 0;
                        state.selected_control_index = None;
                    });
                    if let Err(error) = result {
                        self.error("E303", error);
                        self.enter(Phase::Error, "Disconnect failed. Check device");
                    } else {
                        self.clear_error();
                        self.enter(destination, "Looking for devices");
                    }
                }
            }
        }
        self.dispatch_output();
        let now = Instant::now();
        if self.deferred.is_some_and(|(at, _)| now >= at) {
            let (_, event) = self.deferred.take().unwrap();
            self.input(event);
        }
        if self.flow.search_due(now) {
            self.start_scan();
        }
        if self.retry_at.is_some_and(|at| now >= at) {
            self.retry_at = None;
            if self.flow.retry(now) {
                let status = format!(
                    "Reconnecting - attempt {} of {}",
                    self.flow.attempt,
                    crate::flow::MAX_RECONNECT_ATTEMPTS
                );
                rad_ble::with_state(&self.state, |state| {
                    state.status = status;
                });
                self.approve_target();
            }
        }
        if self.flow.phase.waiting() && self.retry_at.is_none() {
            let (error_code, detail) = {
                let state = rad_ble::read_state(&self.state);
                (state.error_code.clone(), state.last_error.clone())
            };
            if let Some(code) =
                error_code.filter(|code| matches!(code.as_str(), "E201" | "E202" | "E204" | "E206"))
            {
                self.connection_failed(&code, detail.unwrap_or_default());
            } else if self.flow.timed_out(now) {
                self.connection_failed(
                    "E203",
                    "Device setup timed out. Keep it nearby and close other control apps."
                        .to_owned(),
                );
            }
        }
        if self.flow.phase.waiting() || self.flow.phase == Phase::Disconnecting {
            rad_ble::with_state(&self.state, |state| {
                state.elapsed_seconds = now.duration_since(self.flow.entered).as_secs();
                if self.flow.phase == Phase::Disconnecting && state.elapsed_seconds >= 5 {
                    state.status = "Waiting for Bluetooth to finish".to_owned();
                }
            });
        }
    }
}

async fn bounded<T, E: std::fmt::Display>(
    future: impl std::future::Future<Output = Result<T, E>>,
) -> Result<T, String> {
    tokio::time::timeout(COMMAND_TIMEOUT, future)
        .await
        .map_err(|_| "Device command timed out".to_owned())?
        .map_err(|error| error.to_string())
}

#[cfg(all(test, not(target_os = "espidf")))]
mod tests {
    use super::*;
    fn candidate(index: usize) -> Esp32BleCandidate {
        Esp32BleCandidate {
            name: format!("BLE-{index}"),
            display_name: format!("Device {index}"),
            address: format!("address-{index}"),
            protocols: vec![],
        }
    }
    fn controller() -> Controller {
        Controller::new(
            Arc::new(ButtplugClient::new("test")),
            Esp32BleCandidateApprover::default(),
            rad_ble::shared_state(),
        )
    }
    #[test]
    fn refresh_preserves_selection_by_address_and_removes_absent_devices() {
        let mut app = controller();
        app.discovery(DiscoverySnapshot {
            candidates: vec![candidate(1), candidate(2), candidate(3)],
            complete: true,
        });
        app.selected = 1;
        app.discovery(DiscoverySnapshot {
            candidates: vec![candidate(3), candidate(2)],
            complete: true,
        });
        assert_eq!(app.candidates.len(), 2);
        assert_eq!(app.candidates[app.selected].address, "address-2");
        app.discovery(DiscoverySnapshot {
            candidates: vec![],
            complete: true,
        });
        assert_eq!(app.selected, 0);
        assert!(app.candidates.is_empty());
    }
    #[test]
    fn browsing_holds_list_stable_and_encoder_wraps_all_detents() {
        let mut app = controller();
        app.discovery(DiscoverySnapshot {
            candidates: (0..1000).map(candidate).collect(),
            complete: true,
        });
        app.physical_left(-1);
        assert_eq!(app.selected, 999);
        app.physical_left(3);
        assert_eq!(app.selected, 2);
        app.discovery(DiscoverySnapshot {
            candidates: vec![],
            complete: true,
        });
        assert_eq!(app.candidates.len(), 1000);
    }
    #[test]
    fn only_right_button_selects_and_connection_page_ignores_navigation() {
        let mut app = controller();
        app.discovery(DiscoverySnapshot {
            candidates: vec![candidate(1), candidate(2)],
            complete: true,
        });
        app.input(RadBleControlEvent::Right);
        assert_eq!(app.flow.phase, Phase::Connecting);
        let generation = app.generation;
        app.input(RadBleControlEvent::Right);
        app.physical_left(1);
        assert_eq!(app.generation, generation);
        assert_eq!(app.selected, 0);
        app.discovery(DiscoverySnapshot {
            candidates: vec![candidate(3)],
            complete: true,
        });
        assert_eq!(app.candidates.len(), 2);
    }
    #[test]
    fn setup_timeout_exits_with_an_error_code_and_invalidates_connection() {
        let mut app = controller();
        app.discovery(DiscoverySnapshot {
            candidates: vec![candidate(1)],
            complete: true,
        });
        app.input(RadBleControlEvent::Right);
        app.flow.entered = Instant::now() - crate::flow::CONNECT_TIMEOUT;
        app.tick();
        assert_eq!(app.flow.phase, Phase::Error);
        assert_eq!(
            rad_ble::screen_state(&app.state).error_code.as_deref(),
            Some("E203")
        );
        assert!(!app.approver.has_live_connection(app.generation));
    }
    #[test]
    fn late_output_completion_does_not_finish_a_newer_stop() {
        let mut app = controller();
        app.enter(Phase::Connected, "Connected");
        let output = app.commands.advance();
        app.commands.advance();
        app.command_pending = true;
        app.sender
            .send(Completion::Output {
                sequence: output,
                generation: app.generation,
                index: 0,
                value: 100,
                result: Ok(()),
            })
            .unwrap();
        app.tick();
        assert!(app.command_pending);
    }
    #[test]
    fn reconnect_exhaustion_keeps_controls_visible_and_disabled() {
        let mut app = controller();
        app.enter(Phase::Reconnecting, "Reconnecting");
        app.flow.attempt = crate::flow::MAX_RECONNECT_ATTEMPTS;
        rad_ble::with_state(&app.state, |state| {
            state.connected_name = Some("Device".into());
            state.controls = vec![crate::screen::RadBleControlSetting {
                feature_index: 0,
                label: "Vibration".into(),
                output_type: "Vibrate".into(),
                value_percent: 25,
                minimum_percent: 0,
            }];
        });
        app.connection_failed("E203", "Timed out".into());
        let screen = rad_ble::screen_state(&app.state);
        assert_eq!(screen.phase, "error");
        assert_eq!(screen.controls.len(), 1);
        assert!(screen.connected_name.is_some());
        assert!(app.retry_at.is_none());
    }
    #[test]
    fn deferred_input_does_not_block_and_is_cancelled_by_new_input() {
        let mut app = controller();
        app.input(RadBleControlEvent::Middle { defer_ms: 5000 });
        assert!(app.deferred.is_some());
        app.input(RadBleControlEvent::Left);
        assert_eq!(app.flow.phase, Phase::Menu);
        assert!(app.deferred.is_none());
    }
}

#[cfg(all(test, not(target_os = "espidf")))]
mod upstream_integration_tests {
    use super::*;
    use buttplug_client_in_process::ButtplugInProcessClientConnectorBuilder;
    use buttplug_server::{ButtplugServerBuilder, device::ServerDeviceManagerBuilder};
    use buttplug_server_device_config::{SimulatedDeviceConfigEntry, load_protocol_configs};
    use futures::StreamExt;
    use std::sync::atomic::Ordering;

    #[test]
    fn official_server_enumerates_controls_accepts_both_directions_and_stops() {
        let runtime = tokio::runtime::Builder::new_current_thread()
            .enable_time()
            .build()
            .unwrap();
        runtime.block_on(async {
            for archetype in ["simulated-2vibe", "simulated-rotator", "simulated-stroker"] {
                let mut config = load_protocol_configs(&None, &None, false).unwrap();
                config
                    .add_simulated_devices(vec![SimulatedDeviceConfigEntry::new(archetype, None)]);
                let mut manager = ServerDeviceManagerBuilder::new(config.finish().unwrap());
                manager.add_simulated_devices_if_configured();
                let server = ButtplugServerBuilder::new(manager.finish().unwrap())
                    .finish()
                    .unwrap();
                let mut connector = ButtplugInProcessClientConnectorBuilder::default();
                connector.server(server);
                let client = Arc::new(ButtplugClient::new("controller integration test"));
                client.connect(connector.finish()).await.unwrap();
                let mut events = client.event_stream();
                let approver = Esp32BleCandidateApprover::default();
                let mut app =
                    Controller::new(client.clone(), approver.clone(), rad_ble::shared_state());
                app.candidates.push(Esp32BleCandidate {
                    name: archetype.into(),
                    display_name: archetype.into(),
                    address: "simulated".into(),
                    protocols: vec![],
                });
                app.input(RadBleControlEvent::Right);
                approver.live.store(true, Ordering::SeqCst);
                client.start_scanning().await.unwrap();
                tokio::time::timeout(Duration::from_secs(3), async {
                    while app.flow.phase != Phase::Connected {
                        tokio::select! {
                            Some(event)=events.next()=>app.client_event(event),
                            _=tokio::time::sleep(Duration::from_millis(1))=>app.tick(),
                        }
                    }
                })
                .await
                .expect("initial stop must complete before enabling controls");
                assert!(!app.controls.is_empty());
                for index in 0..app.controls.len() {
                    app.selected_control = index;
                    // Rapid turns while the first command is pending must preserve all detents.
                    app.input(RadBleControlEvent::EncoderLeft { delta: 10 });
                    app.input(RadBleControlEvent::EncoderLeft { delta: 15 });
                    settle(&mut app).await;
                    assert_eq!(app.controls[index].value_percent, 25);
                    if app.controls[index].minimum_percent < 0 {
                        app.input(RadBleControlEvent::EncoderLeft { delta: -50 });
                        settle(&mut app).await;
                        assert_eq!(app.controls[index].value_percent, -25);
                    }
                }
                app.input(RadBleControlEvent::Middle { defer_ms: 0 });
                settle(&mut app).await;
                assert!(
                    app.controls
                        .iter()
                        .all(|control| control.value_percent == 0)
                );
                let device = app.device.clone().unwrap();
                app.client_event(ButtplugClientEvent::DeviceRemoved(device.clone()));
                assert_eq!(app.flow.phase, Phase::Reconnecting);
                assert!(!app.controls.is_empty());
                app.input(RadBleControlEvent::EncoderLeft { delta: 100 });
                assert!(
                    app.controls
                        .iter()
                        .all(|control| control.value_percent == 0)
                );
                app.input(RadBleControlEvent::Left);
                app.client_event(ButtplugClientEvent::DeviceAdded(device));
                assert_eq!(app.flow.phase, Phase::Disconnecting);
                client.disconnect().await.unwrap();
            }
        });
    }
    async fn settle(app: &mut Controller) {
        tokio::time::timeout(Duration::from_secs(3), async {
            loop {
                tokio::time::sleep(Duration::from_millis(1)).await;
                app.tick();
                assert_eq!(app.flow.phase, Phase::Connected);
                if !app.command_pending {
                    break;
                }
            }
        })
        .await
        .expect("upstream command must finish");
    }
}
