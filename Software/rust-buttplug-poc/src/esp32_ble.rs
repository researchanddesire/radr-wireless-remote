use async_trait::async_trait;
use buttplug_core::{ButtplugResultFuture, errors::ButtplugDeviceError};
use buttplug_server::device::hardware::{
    Hardware, HardwareConnector, HardwareEvent, HardwareInternal, HardwareReadCmd, HardwareReading,
    HardwareSpecializer, HardwareSubscribeCmd, HardwareUnsubscribeCmd, HardwareWriteCmd,
    communication::{
        HardwareCommunicationManager, HardwareCommunicationManagerBuilder,
        HardwareCommunicationManagerEvent,
    },
};
use buttplug_server_device_config::{
    BluetoothLESpecifier, DeviceConfigurationManager, Endpoint, ProtocolCommunicationSpecifier,
};
use esp32_nimble::{
    BLEAddress, BLEAddressType, BLEClient, BLEDevice, BLERemoteCharacteristic, BLEScan,
    utilities::BleUuid,
};
use futures::{FutureExt, future, future::BoxFuture};
use std::{
    collections::{HashMap, HashSet},
    fmt,
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicU32, Ordering},
        mpsc::{self, Receiver, Sender, TryRecvError},
    },
    time::{Duration, Instant},
};
use tokio::sync::{broadcast, mpsc as tokio_mpsc, oneshot};
use uuid::Uuid;

const SCAN_SLICE_MS: i32 = 500;
const BLE_WORKER_STACK_BYTES: usize = 16 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Esp32BleAddress {
    little_endian_bytes: [u8; 6],
    address_type: u8,
}

impl Esp32BleAddress {
    fn from_nimble(address: BLEAddress) -> Self {
        Self {
            little_endian_bytes: address.as_le_bytes(),
            address_type: address.addr_type() as u8,
        }
    }

    fn to_nimble(self) -> Result<BLEAddress, String> {
        let address_type = match self.address_type {
            0 => BLEAddressType::Public,
            1 => BLEAddressType::Random,
            2 => BLEAddressType::PublicID,
            3 => BLEAddressType::RandomID,
            value => return Err(format!("unsupported BLE address type {value}")),
        };
        Ok(BLEAddress::from_le_bytes(
            self.little_endian_bytes,
            address_type,
        ))
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct Advertisement {
    name: String,
    address: Esp32BleAddress,
    address_string: String,
    manufacturer_data: HashMap<u16, Vec<u8>>,
    advertised_services: Vec<Uuid>,
}

#[derive(Debug)]
enum BleWorkerCommand {
    StartScanning,
    StopScanning,
    ApproveCandidate {
        address_string: String,
    },
    Connect {
        address: Esp32BleAddress,
        address_string: String,
        events: broadcast::Sender<HardwareEvent>,
        reply: oneshot::Sender<Result<u32, String>>,
    },
    Specialize {
        session_id: u32,
        services: Vec<(Uuid, Vec<(Endpoint, Uuid)>)>,
        reply: oneshot::Sender<Result<Vec<Endpoint>, String>>,
    },
    Read {
        session_id: u32,
        endpoint: Endpoint,
        reply: oneshot::Sender<Result<Vec<u8>, String>>,
    },
    Write {
        session_id: u32,
        endpoint: Endpoint,
        data: Vec<u8>,
        with_response: bool,
        reply: oneshot::Sender<Result<(), String>>,
    },
    Subscribe {
        session_id: u32,
        endpoint: Endpoint,
        reply: oneshot::Sender<Result<(), String>>,
    },
    Unsubscribe {
        session_id: u32,
        endpoint: Endpoint,
        reply: oneshot::Sender<Result<(), String>>,
    },
    Disconnect {
        session_id: u32,
        reply: oneshot::Sender<Result<(), String>>,
    },
}

struct ConnectedDevice {
    // `esp32-nimble` passes the BLEClient object's address to NimBLE's C
    // callback API. Keep the object boxed so moving this wrapper cannot
    // invalidate that address.
    client: Box<BLEClient>,
    address: String,
    events: broadcast::Sender<HardwareEvent>,
    endpoints: HashMap<Endpoint, BLERemoteCharacteristic>,
    disconnected_since: Option<Instant>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Esp32BleCandidate {
    pub name: String,
    pub address: String,
    pub protocols: Vec<String>,
    pub advertised_services: Vec<Uuid>,
    pub manufacturer_ids: Vec<u16>,
}

#[derive(Clone)]
pub struct Esp32BleCandidateApprover {
    commands: Sender<BleWorkerCommand>,
}

impl Esp32BleCandidateApprover {
    pub fn approve(&self, address: &str) -> Result<(), String> {
        self.commands
            .send(BleWorkerCommand::ApproveCandidate {
                address_string: address.to_owned(),
            })
            .map_err(|_| "RADR ESP32 BLE worker is no longer running".to_owned())
    }
}

#[derive(Clone)]
struct BleWorkerHandle {
    commands: Sender<BleWorkerCommand>,
    scanning: Arc<AtomicBool>,
    available: Arc<AtomicBool>,
}

impl BleWorkerHandle {
    fn send(&self, command: BleWorkerCommand) -> Result<(), ButtplugDeviceError> {
        self.commands.send(command).map_err(|_| {
            ButtplugDeviceError::DeviceConnectionError(
                "RADR ESP32 BLE worker is no longer running".to_owned(),
            )
        })
    }
}

pub struct Esp32BleCommunicationManagerBuilder {
    device_configuration: Arc<DeviceConfigurationManager>,
    candidate_sender: tokio_mpsc::Sender<Esp32BleCandidate>,
    commands: Sender<BleWorkerCommand>,
    command_receiver: Option<Receiver<BleWorkerCommand>>,
}

impl Esp32BleCommunicationManagerBuilder {
    pub fn new(
        device_configuration: Arc<DeviceConfigurationManager>,
        candidate_sender: tokio_mpsc::Sender<Esp32BleCandidate>,
    ) -> (Self, Esp32BleCandidateApprover) {
        let (commands, command_receiver) = mpsc::channel();
        let approver = Esp32BleCandidateApprover {
            commands: commands.clone(),
        };
        (
            Self {
                device_configuration,
                candidate_sender,
                commands,
                command_receiver: Some(command_receiver),
            },
            approver,
        )
    }
}

impl HardwareCommunicationManagerBuilder for Esp32BleCommunicationManagerBuilder {
    fn finish(
        &mut self,
        event_sender: tokio_mpsc::Sender<HardwareCommunicationManagerEvent>,
    ) -> Box<dyn HardwareCommunicationManager> {
        let command_sender = self.commands.clone();
        let command_receiver = self
            .command_receiver
            .take()
            .expect("ESP32 BLE communication manager builder can only be finished once");
        let scanning = Arc::new(AtomicBool::new(false));
        let available = Arc::new(AtomicBool::new(false));
        let scanning_for_worker = scanning.clone();
        let available_for_worker = available.clone();
        let command_sender_for_worker = command_sender.clone();
        let device_configuration = self.device_configuration.clone();
        let candidate_sender = self.candidate_sender.clone();
        let thread_result = std::thread::Builder::new()
            .name("radr-ble".to_owned())
            .stack_size(BLE_WORKER_STACK_BYTES)
            .spawn(move || {
                run_ble_worker(
                    command_receiver,
                    command_sender_for_worker,
                    event_sender,
                    scanning_for_worker,
                    available_for_worker,
                    device_configuration,
                    candidate_sender,
                );
            });

        if let Err(error) = thread_result {
            log::error!("Could not start RADR ESP32 BLE worker: {error}");
        } else {
            available.store(true, Ordering::Release);
        }

        Box::new(Esp32BleCommunicationManager {
            worker: BleWorkerHandle {
                commands: command_sender,
                scanning,
                available,
            },
        })
    }
}

pub struct Esp32BleCommunicationManager {
    worker: BleWorkerHandle,
}

impl HardwareCommunicationManager for Esp32BleCommunicationManager {
    fn name(&self) -> &'static str {
        "Esp32BleCommunicationManager"
    }

    fn start_scanning(&mut self) -> ButtplugResultFuture {
        self.worker.scanning.store(true, Ordering::Release);
        let result = self.worker.send(BleWorkerCommand::StartScanning);
        if let Err(error) = result {
            self.worker.scanning.store(false, Ordering::Release);
            return future::ready(Err(error.into())).boxed();
        }
        future::ready(Ok(())).boxed()
    }

    fn stop_scanning(&mut self) -> ButtplugResultFuture {
        self.worker.scanning.store(false, Ordering::Release);
        let result = self.worker.send(BleWorkerCommand::StopScanning);
        match result {
            Ok(()) => future::ready(Ok(())).boxed(),
            Err(error) => future::ready(Err(error.into())).boxed(),
        }
    }

    fn scanning_status(&self) -> bool {
        self.worker.scanning.load(Ordering::Acquire)
    }

    fn can_scan(&self) -> bool {
        self.worker.available.load(Ordering::Acquire)
    }
}

#[derive(Clone)]
struct Esp32BleConnector {
    advertisement: Advertisement,
    worker: BleWorkerHandle,
}

impl fmt::Debug for Esp32BleConnector {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("Esp32BleConnector")
            .field("name", &self.advertisement.name)
            .field("address", &self.advertisement.address_string)
            .finish()
    }
}

#[async_trait]
impl HardwareConnector for Esp32BleConnector {
    fn specifier(&self) -> ProtocolCommunicationSpecifier {
        ProtocolCommunicationSpecifier::BluetoothLE(BluetoothLESpecifier::new_from_device(
            &self.advertisement.name,
            &self.advertisement.manufacturer_data,
            &self.advertisement.advertised_services,
        ))
    }

    async fn connect(&mut self) -> Result<Box<dyn HardwareSpecializer>, ButtplugDeviceError> {
        let (event_sender, _) = broadcast::channel(256);
        let (reply_sender, reply_receiver) = oneshot::channel();
        self.worker.send(BleWorkerCommand::Connect {
            address: self.advertisement.address,
            address_string: self.advertisement.address_string.clone(),
            events: event_sender.clone(),
            reply: reply_sender,
        })?;

        let session_id = receive_worker_reply(reply_receiver, "connect").await?;
        Ok(Box::new(Esp32BleSpecializer {
            worker: self.worker.clone(),
            session_id,
            name: self.advertisement.name.clone(),
            address: self.advertisement.address_string.clone(),
            events: event_sender,
        }))
    }
}

struct Esp32BleSpecializer {
    worker: BleWorkerHandle,
    session_id: u32,
    name: String,
    address: String,
    events: broadcast::Sender<HardwareEvent>,
}

#[async_trait]
impl HardwareSpecializer for Esp32BleSpecializer {
    async fn specialize(
        &mut self,
        specifiers: &[ProtocolCommunicationSpecifier],
    ) -> Result<Hardware, ButtplugDeviceError> {
        let bluetooth = specifiers
            .iter()
            .find_map(|specifier| match specifier {
                ProtocolCommunicationSpecifier::BluetoothLE(value) => Some(value),
                _ => None,
            })
            .ok_or_else(|| {
                ButtplugDeviceError::DeviceConfigurationError(
                    "matched protocol did not provide a Bluetooth LE specifier".to_owned(),
                )
            })?;

        let services = bluetooth
            .services()
            .iter()
            .map(|(service_uuid, endpoints)| {
                (
                    *service_uuid,
                    endpoints
                        .iter()
                        .map(|(endpoint, characteristic_uuid)| (*endpoint, *characteristic_uuid))
                        .collect(),
                )
            })
            .collect();

        let (reply_sender, reply_receiver) = oneshot::channel();
        self.worker.send(BleWorkerCommand::Specialize {
            session_id: self.session_id,
            services,
            reply: reply_sender,
        })?;
        let endpoints = receive_worker_reply(reply_receiver, "specialize").await?;

        Ok(Hardware::new(
            &self.name,
            &self.address,
            &endpoints,
            &Some(Duration::from_millis(75)),
            false,
            Box::new(Esp32BleHardware {
                worker: self.worker.clone(),
                session_id: self.session_id,
                events: self.events.clone(),
            }),
        ))
    }
}

struct Esp32BleHardware {
    worker: BleWorkerHandle,
    session_id: u32,
    events: broadcast::Sender<HardwareEvent>,
}

impl HardwareInternal for Esp32BleHardware {
    fn disconnect(&self) -> BoxFuture<'static, Result<(), ButtplugDeviceError>> {
        let worker = self.worker.clone();
        let session_id = self.session_id;
        async move {
            let (reply_sender, reply_receiver) = oneshot::channel();
            worker.send(BleWorkerCommand::Disconnect {
                session_id,
                reply: reply_sender,
            })?;
            receive_worker_reply(reply_receiver, "disconnect").await
        }
        .boxed()
    }

    fn event_stream(&self) -> broadcast::Receiver<HardwareEvent> {
        self.events.subscribe()
    }

    fn read_value(
        &self,
        message: &HardwareReadCmd,
    ) -> BoxFuture<'static, Result<HardwareReading, ButtplugDeviceError>> {
        let worker = self.worker.clone();
        let session_id = self.session_id;
        let endpoint = message.endpoint();
        async move {
            let (reply_sender, reply_receiver) = oneshot::channel();
            worker.send(BleWorkerCommand::Read {
                session_id,
                endpoint,
                reply: reply_sender,
            })?;
            let data = receive_worker_reply(reply_receiver, "read").await?;
            Ok(HardwareReading::new(endpoint, &data))
        }
        .boxed()
    }

    fn write_value(
        &self,
        message: &HardwareWriteCmd,
    ) -> BoxFuture<'static, Result<(), ButtplugDeviceError>> {
        let worker = self.worker.clone();
        let session_id = self.session_id;
        let endpoint = message.endpoint();
        let data = message.data().clone();
        let with_response = message.write_with_response();
        async move {
            let (reply_sender, reply_receiver) = oneshot::channel();
            worker.send(BleWorkerCommand::Write {
                session_id,
                endpoint,
                data,
                with_response,
                reply: reply_sender,
            })?;
            receive_worker_reply(reply_receiver, "write").await
        }
        .boxed()
    }

    fn subscribe(
        &self,
        message: &HardwareSubscribeCmd,
    ) -> BoxFuture<'static, Result<(), ButtplugDeviceError>> {
        let worker = self.worker.clone();
        let session_id = self.session_id;
        let endpoint = message.endpoint();
        async move {
            let (reply_sender, reply_receiver) = oneshot::channel();
            worker.send(BleWorkerCommand::Subscribe {
                session_id,
                endpoint,
                reply: reply_sender,
            })?;
            receive_worker_reply(reply_receiver, "subscribe").await
        }
        .boxed()
    }

    fn unsubscribe(
        &self,
        message: &HardwareUnsubscribeCmd,
    ) -> BoxFuture<'static, Result<(), ButtplugDeviceError>> {
        let worker = self.worker.clone();
        let session_id = self.session_id;
        let endpoint = message.endpoint();
        async move {
            let (reply_sender, reply_receiver) = oneshot::channel();
            worker.send(BleWorkerCommand::Unsubscribe {
                session_id,
                endpoint,
                reply: reply_sender,
            })?;
            receive_worker_reply(reply_receiver, "unsubscribe").await
        }
        .boxed()
    }
}

async fn receive_worker_reply<T>(
    receiver: oneshot::Receiver<Result<T, String>>,
    operation: &str,
) -> Result<T, ButtplugDeviceError> {
    receiver
        .await
        .map_err(|_| {
            ButtplugDeviceError::DeviceConnectionError(format!(
                "RADR ESP32 BLE worker dropped the {operation} response"
            ))
        })?
        .map_err(|error| {
            ButtplugDeviceError::DeviceConnectionError(format!(
                "RADR ESP32 BLE {operation} failed: {error}"
            ))
        })
}

fn run_ble_worker(
    commands: Receiver<BleWorkerCommand>,
    command_sender: Sender<BleWorkerCommand>,
    event_sender: tokio_mpsc::Sender<HardwareCommunicationManagerEvent>,
    scanning: Arc<AtomicBool>,
    available: Arc<AtomicBool>,
    device_configuration: Arc<DeviceConfigurationManager>,
    candidate_sender: tokio_mpsc::Sender<Esp32BleCandidate>,
) {
    let ble_device = BLEDevice::take();
    if let Err(error) = ble_device.set_preferred_mtu(512) {
        log::warn!("Could not set preferred BLE MTU: {error:?}");
    }
    available.store(true, Ordering::Release);
    log::info!("RADR ESP32 BLE worker ready");

    let mut seen = HashMap::<String, Advertisement>::new();
    let mut candidate_snapshots = HashMap::<String, Esp32BleCandidate>::new();
    let mut pending_candidates = HashMap::<String, Advertisement>::new();
    let mut reported_addresses = HashSet::<String>::new();
    let mut connections = HashMap::<u32, ConnectedDevice>::new();
    let mut retired_clients = Vec::<(Instant, Box<BLEClient>)>::new();
    let next_session_id = AtomicU32::new(1);
    let configured_manufacturer_ids = configured_manufacturer_ids(&device_configuration);

    loop {
        loop {
            match commands.try_recv() {
                Ok(command) => {
                    if matches!(&command, BleWorkerCommand::StartScanning) {
                        candidate_snapshots.clear();
                        pending_candidates.clear();
                        reported_addresses.clear();
                    }
                    match command {
                        BleWorkerCommand::ApproveCandidate { address_string } => {
                            approve_candidate(
                                &address_string,
                                &pending_candidates,
                                &mut reported_addresses,
                                &event_sender,
                                BleWorkerHandle {
                                    commands: command_sender.clone(),
                                    scanning: scanning.clone(),
                                    available: available.clone(),
                                },
                            );
                        }
                        command => {
                            handle_worker_command(
                                command,
                                ble_device,
                                &mut seen,
                                &mut connections,
                                &mut retired_clients,
                                &next_session_id,
                            );
                        }
                    }
                }
                Err(TryRecvError::Empty) => break,
                Err(TryRecvError::Disconnected) => {
                    available.store(false, Ordering::Release);
                    scanning.store(false, Ordering::Release);
                    return;
                }
            }
        }

        for connection in connections.values_mut() {
            if connection.client.connected() {
                connection.disconnected_since = None;
            } else if connection.disconnected_since.is_none() {
                connection.disconnected_since = Some(Instant::now());
            }
        }
        let retired_addresses: Vec<String> = connections
            .values()
            .filter_map(|connection| {
                connection
                    .disconnected_since
                    .filter(|since| since.elapsed() >= Duration::from_secs(2))
                    .map(|_| connection.address.clone())
            })
            .collect();
        for address in retired_addresses {
            connections.retain(|_, connection| connection.address != address);
            reported_addresses.remove(&address);
        }
        retired_clients.retain(|(retired_at, _)| retired_at.elapsed() < Duration::from_secs(2));

        if !scanning.load(Ordering::Acquire) {
            std::thread::sleep(Duration::from_millis(20));
            continue;
        }

        let scan_result = esp_idf_svc::hal::task::block_on(async {
            let mut scan = BLEScan::new();
            scan.active_scan(true)
                .filter_duplicates(true)
                .interval(100)
                .window(99);
            scan.start(ble_device, SCAN_SLICE_MS, |device, data| {
                let address = device.addr();
                let address_string = format!("{address:?}");
                let name = data
                    .name()
                    .map(|value| {
                        String::from_utf8_lossy(value.as_ref())
                            .trim_end_matches('\0')
                            .to_owned()
                    })
                    .unwrap_or_default();
                let advertised_services: Vec<Uuid> =
                    data.service_uuids().map(nimble_uuid_to_uuid).collect();

                let mut manufacturer_data = HashMap::new();
                if let Some(value) = data.manufacture_data()
                    && configured_manufacturer_ids.contains(&value.company_identifier)
                {
                    manufacturer_data.insert(value.company_identifier, value.payload.to_vec());
                }

                let mut advertisement =
                    seen.get(&address_string)
                        .cloned()
                        .unwrap_or_else(|| Advertisement {
                            name: String::new(),
                            address: Esp32BleAddress::from_nimble(address),
                            address_string: address_string.clone(),
                            manufacturer_data: HashMap::new(),
                            advertised_services: Vec::new(),
                        });
                let previous = advertisement.clone();
                if !name.is_empty() {
                    advertisement.name = name;
                }
                for (company, payload) in manufacturer_data {
                    advertisement.manufacturer_data.insert(company, payload);
                }
                for service in advertised_services {
                    if !advertisement.advertised_services.contains(&service) {
                        advertisement.advertised_services.push(service);
                    }
                }

                if advertisement == previous
                    || (advertisement.name.is_empty()
                        && advertisement.advertised_services.is_empty()
                        && advertisement.manufacturer_data.is_empty())
                {
                    return None::<()>;
                }
                seen.insert(address_string.clone(), advertisement.clone());

                let specifier = ProtocolCommunicationSpecifier::BluetoothLE(
                    BluetoothLESpecifier::new_from_device(
                        &advertisement.name,
                        &advertisement.manufacturer_data,
                        &advertisement.advertised_services,
                    ),
                );
                let matching_protocols = matching_protocols(&device_configuration, &specifier);
                if matching_protocols.is_empty() {
                    pending_candidates.remove(&address_string);
                    return None;
                }

                let mut advertised_services = advertisement.advertised_services.clone();
                advertised_services.sort_unstable();
                let mut manufacturer_ids: Vec<u16> =
                    advertisement.manufacturer_data.keys().copied().collect();
                manufacturer_ids.sort_unstable();
                let candidate = Esp32BleCandidate {
                    name: advertisement.name.clone(),
                    address: address_string.clone(),
                    protocols: matching_protocols,
                    advertised_services,
                    manufacturer_ids,
                };

                pending_candidates.insert(address_string.clone(), advertisement);
                if candidate_snapshots.get(&address_string) != Some(&candidate) {
                    if candidate_sender.try_send(candidate.clone()).is_err() {
                        log::warn!("Could not enqueue a supported ESP32 BLE candidate for RADR");
                    }
                    candidate_snapshots.insert(address_string, candidate);
                }
                None
            })
            .await
        });

        if let Err(error) = scan_result {
            log::error!("ESP32 BLE scan failed: {error:?}");
            scanning.store(false, Ordering::Release);
            let _ = event_sender.try_send(HardwareCommunicationManagerEvent::ScanningFinished);
        }
    }
}

fn approve_candidate(
    address: &str,
    pending_candidates: &HashMap<String, Advertisement>,
    reported_addresses: &mut HashSet<String>,
    event_sender: &tokio_mpsc::Sender<HardwareCommunicationManagerEvent>,
    worker: BleWorkerHandle,
) {
    if reported_addresses.contains(address) {
        log::info!("BLE candidate {address} has already been sent to Buttplug");
        return;
    }

    let Some(advertisement) = pending_candidates.get(address).cloned() else {
        log::warn!("Cannot approve unknown or no-longer-supported BLE candidate {address}");
        return;
    };
    let name = advertisement.name.clone();
    let connector = Esp32BleConnector {
        advertisement,
        worker,
    };
    match event_sender.try_send(HardwareCommunicationManagerEvent::DeviceFound {
        name,
        address: address.to_owned(),
        creator: Box::new(connector),
    }) {
        Ok(()) => {
            reported_addresses.insert(address.to_owned());
            log::info!("Approved BLE candidate {address} for Buttplug connection");
        }
        Err(_) => {
            log::warn!("Could not enqueue approved ESP32 BLE candidate for Buttplug");
        }
    }
}

fn configured_manufacturer_ids(device_configuration: &DeviceConfigurationManager) -> HashSet<u16> {
    let mut ids = HashSet::new();

    for entry in device_configuration.user_communication_specifiers().iter() {
        collect_manufacturer_ids(entry.value(), &mut ids);
    }
    for specifiers in device_configuration
        .base_communication_specifiers()
        .values()
    {
        collect_manufacturer_ids(specifiers, &mut ids);
    }

    ids
}

fn collect_manufacturer_ids(specifiers: &[ProtocolCommunicationSpecifier], ids: &mut HashSet<u16>) {
    for specifier in specifiers {
        if let ProtocolCommunicationSpecifier::BluetoothLE(bluetooth) = specifier {
            ids.extend(
                bluetooth
                    .manufacturer_data()
                    .iter()
                    .map(|data| *data.company()),
            );
        }
    }
}

fn matching_protocols(
    device_configuration: &DeviceConfigurationManager,
    candidate: &ProtocolCommunicationSpecifier,
) -> Vec<String> {
    let mut matches = Vec::new();

    for entry in device_configuration.user_communication_specifiers().iter() {
        if entry.value().contains(candidate) {
            matches.push(entry.key().clone());
        }
    }
    for (protocol, specifiers) in device_configuration.base_communication_specifiers() {
        if specifiers.contains(candidate) {
            matches.push(protocol.clone());
        }
    }

    matches.sort_unstable();
    matches.dedup();
    matches
}

fn nimble_uuid_to_uuid(value: BleUuid) -> Uuid {
    const BLUETOOTH_BASE_UUID: u128 = 0x00000000_0000_1000_8000_00805f9b34fb;

    match value {
        BleUuid::Uuid16(value) => Uuid::from_u128(((value as u128) << 96) | BLUETOOTH_BASE_UUID),
        BleUuid::Uuid32(value) => Uuid::from_u128(((value as u128) << 96) | BLUETOOTH_BASE_UUID),
        BleUuid::Uuid128(little_endian_bytes) => {
            Uuid::from_u128(u128::from_le_bytes(little_endian_bytes))
        }
    }
}

fn handle_worker_command(
    command: BleWorkerCommand,
    ble_device: &BLEDevice,
    seen: &mut HashMap<String, Advertisement>,
    connections: &mut HashMap<u32, ConnectedDevice>,
    retired_clients: &mut Vec<(Instant, Box<BLEClient>)>,
    next_session_id: &AtomicU32,
) {
    match command {
        BleWorkerCommand::StartScanning => seen.clear(),
        BleWorkerCommand::StopScanning => {}
        BleWorkerCommand::ApproveCandidate { .. } => {
            unreachable!("approval commands are handled by the BLE worker loop")
        }
        BleWorkerCommand::Connect {
            address,
            address_string,
            events,
            reply,
        } => {
            let result = esp_idf_svc::hal::task::block_on(async {
                let address = address.to_nimble()?;
                let mut client = Box::new(ble_device.new_client());
                let disconnect_events = events.clone();
                let disconnect_address = address_string.clone();
                client.on_disconnect(move |_| {
                    let _ = disconnect_events
                        .send(HardwareEvent::Disconnected(disconnect_address.clone()));
                });
                if let Err(error) = client.connect(&address).await {
                    retired_clients.push((Instant::now(), client));
                    return Err(format!("{error:?}"));
                }

                let session_id = next_session_id.fetch_add(1, Ordering::Relaxed);
                connections.insert(
                    session_id,
                    ConnectedDevice {
                        client,
                        address: address_string,
                        events,
                        endpoints: HashMap::new(),
                        disconnected_since: None,
                    },
                );
                Ok(session_id)
            });
            log_worker_memory("connect");
            let _ = reply.send(result);
        }
        BleWorkerCommand::Specialize {
            session_id,
            services,
            reply,
        } => {
            let result = esp_idf_svc::hal::task::block_on(async {
                let connection = connection_mut(connections, session_id)?;
                let mut endpoints = HashMap::new();
                for (service_uuid, characteristic_map) in services {
                    let Ok(service) = connection
                        .client
                        .get_service(BleUuid::from(service_uuid))
                        .await
                    else {
                        continue;
                    };
                    for (endpoint, characteristic_uuid) in characteristic_map {
                        if let Ok(characteristic) = service
                            .get_characteristic(BleUuid::from(characteristic_uuid))
                            .await
                        {
                            endpoints.insert(endpoint, characteristic.clone());
                        }
                    }
                }
                if endpoints.is_empty() {
                    return Err("none of the protocol's GATT endpoints were found".to_owned());
                }
                let endpoint_names = endpoints.keys().copied().collect();
                connection.endpoints = endpoints;
                Ok(endpoint_names)
            });
            log_worker_memory("GATT specialization");
            let _ = reply.send(result);
        }
        BleWorkerCommand::Read {
            session_id,
            endpoint,
            reply,
        } => {
            let result = esp_idf_svc::hal::task::block_on(async {
                let characteristic = characteristic_mut(connections, session_id, endpoint)?;
                characteristic
                    .read_value()
                    .await
                    .map_err(|error| format!("{error:?}"))
            });
            let _ = reply.send(result);
        }
        BleWorkerCommand::Write {
            session_id,
            endpoint,
            data,
            with_response,
            reply,
        } => {
            let result = esp_idf_svc::hal::task::block_on(async {
                let characteristic = characteristic_mut(connections, session_id, endpoint)?;
                characteristic
                    .write_value(&data, with_response)
                    .await
                    .map_err(|error| format!("{error:?}"))
            });
            let _ = reply.send(result);
        }
        BleWorkerCommand::Subscribe {
            session_id,
            endpoint,
            reply,
        } => {
            let result = esp_idf_svc::hal::task::block_on(async {
                let connection = connection_mut(connections, session_id)?;
                let address = connection.address.clone();
                let events = connection.events.clone();
                let characteristic = connection
                    .endpoints
                    .get_mut(&endpoint)
                    .ok_or_else(|| format!("endpoint {endpoint} was not specialized"))?;
                characteristic.on_notify(move |data| {
                    let _ = events.send(HardwareEvent::Notification(
                        address.clone(),
                        endpoint,
                        data.to_vec(),
                    ));
                });
                if characteristic.can_notify() {
                    characteristic
                        .subscribe_notify(true)
                        .await
                        .map_err(|error| format!("{error:?}"))
                } else if characteristic.can_indicate() {
                    characteristic
                        .subscribe_indicate(true)
                        .await
                        .map_err(|error| format!("{error:?}"))
                } else {
                    Err(format!("endpoint {endpoint} cannot notify or indicate"))
                }
            });
            let _ = reply.send(result);
        }
        BleWorkerCommand::Unsubscribe {
            session_id,
            endpoint,
            reply,
        } => {
            let result = esp_idf_svc::hal::task::block_on(async {
                let characteristic = characteristic_mut(connections, session_id, endpoint)?;
                characteristic
                    .unsubscribe(true)
                    .await
                    .map_err(|error| format!("{error:?}"))
            });
            let _ = reply.send(result);
        }
        BleWorkerCommand::Disconnect { session_id, reply } => {
            let result = connection_mut(connections, session_id).and_then(|connection| {
                connection
                    .client
                    .disconnect()
                    .map_err(|error| format!("{error:?}"))
            });
            let _ = reply.send(result);
        }
    }
}

fn log_worker_memory(stage: &str) {
    let (internal, external, stack_headroom) = unsafe {
        (
            esp_idf_svc::sys::esp_get_free_internal_heap_size(),
            esp_idf_svc::sys::heap_caps_get_free_size(esp_idf_svc::sys::MALLOC_CAP_SPIRAM),
            esp_idf_svc::sys::uxTaskGetStackHighWaterMark(core::ptr::null_mut()),
        )
    };
    log::info!(
        "BLE worker memory after {stage}: internal={internal} external={external} stack_headroom={stack_headroom}"
    );
}

fn connection_mut(
    connections: &mut HashMap<u32, ConnectedDevice>,
    session_id: u32,
) -> Result<&mut ConnectedDevice, String> {
    connections
        .get_mut(&session_id)
        .ok_or_else(|| format!("BLE session {session_id} is not connected"))
}

fn characteristic_mut(
    connections: &mut HashMap<u32, ConnectedDevice>,
    session_id: u32,
    endpoint: Endpoint,
) -> Result<&mut BLERemoteCharacteristic, String> {
    connection_mut(connections, session_id)?
        .endpoints
        .get_mut(&endpoint)
        .ok_or_else(|| format!("endpoint {endpoint} was not specialized"))
}
