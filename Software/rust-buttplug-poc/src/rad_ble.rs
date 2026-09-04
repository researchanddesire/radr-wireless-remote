pub use crate::screen::{RadBleCandidateSummary, RadBleControlSetting, RadBleScreenState};
use base64::{Engine, engine::general_purpose::STANDARD as BASE64};
use esp32_nimble::{
    BLEAdvertisementData, BLECharacteristic, BLEDevice, NimbleProperties,
    utilities::mutex::Mutex as NimbleMutex, uuid128,
};
use serde_json::{Value, json};
use std::{sync::Arc, thread, time::Duration};
use tokio::sync::mpsc;

pub const SERVICE_UUID: &str = "522b443a-5241-4452-0001-420badbabe69";
const LEASE_ID: u32 = 0x5241_4452;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RadBleControlEvent {
    Left,
    Middle { defer_ms: u32 },
    Right,
    EncoderLeft { delta: i32 },
    EncoderRight { delta: i32 },
    Reset,
}

pub use crate::runtime_state::*;

fn runtime_state_json(state: &SharedRadBleState) -> Value {
    let state = read_state(state);
    json!({
        "v": 1,
        "state": state.phase,
        "status": state.status,
        "errorCode": state.error_code,
        "elapsedSeconds": state.elapsed_seconds,
        "controlsEnabled": state.phase == "connected",
        "buttplug": {
            "candidateCount": state.candidate_count,
            "selectedIndex": state.selected_index,
            "selectedName": state.selected_name,
            "selectedProtocols": state.selected_protocols,
            "candidates": state.candidates,
            "connectedName": state.connected_name,
            "featureCount": state.feature_count,
            "features": state.features,
            "controlCount": state.controls.len(),
            "selectedControlIndex": state.selected_control_index,
            "controls": state.controls,
            "controlRevision": state.control_revision,
            "lastConnectedName": state.last_connected_name,
            "lastFeatureCount": state.last_feature_count,
            "lastFeatures": state.last_features,
            "lastError": state.last_error,
        },
        "lease": {
            "active": state.lease_active,
            "expiresInMs": if state.lease_active { 30000 } else { 0 },
        },
    })
}

fn fnv1a_json(values: &[Value]) -> String {
    let bytes = serde_json::to_vec(values).expect("Buttplug feature state must serialize");
    let hash = bytes.iter().fold(0x811c_9dc5_u32, |hash, byte| {
        (hash ^ u32::from(*byte)).wrapping_mul(0x0100_0193)
    });
    format!("{hash:08x}")
}

fn runtime_state_summary_json(state: &SharedRadBleState) -> Value {
    let state = read_state(state);
    json!({
        "v": 1,
        "state": state.phase,
        "status": state.status,
        "errorCode": state.error_code,
        "elapsedSeconds": state.elapsed_seconds,
        "controlsEnabled": state.phase == "connected",
        "buttplug": {
            "candidateCount": state.candidate_count,
            "selectedIndex": state.selected_index,
            "selectedName": state.selected_name,
            "selectedProtocols": state.selected_protocols,
            "connectedName": state.connected_name,
            "featureCount": state.feature_count,
            "featureHash": fnv1a_json(&state.features),
            "controlCount": state.controls.len(),
            "selectedControlIndex": state.selected_control_index,
            "selectedControlValue": state
                .selected_control_index
                .and_then(|index| state.controls.get(index))
                .map(|control| control.value_percent),
            "controlRevision": state.control_revision,
            "lastConnectedName": state.last_connected_name,
            "lastFeatureCount": state.last_feature_count,
            "lastFeatureHash": fnv1a_json(&state.last_features),
            "lastError": state.last_error,
        },
        "lease": {
            "active": state.lease_active,
            "expiresInMs": if state.lease_active { 30000 } else { 0 },
        },
        "summary": true,
    })
}

fn request_wants_summary(request: &Value) -> bool {
    request
        .get("args")
        .and_then(|arguments| arguments.get("summary"))
        .and_then(Value::as_bool)
        .unwrap_or(false)
}

fn resources() -> Vec<Value> {
    vec![
        resource(
            "left",
            "button.left",
            "button",
            "bool",
            false,
            true,
            json!({"events": ["click"]}),
        ),
        resource(
            "middle",
            "button.middle",
            "button",
            "bool",
            false,
            true,
            json!({"events": ["click"]}),
        ),
        resource(
            "right",
            "button.right",
            "button",
            "bool",
            false,
            true,
            json!({"events": ["click"]}),
        ),
        resource(
            "left_encoder",
            "encoder.left",
            "encoder",
            "int",
            false,
            true,
            json!({"operations": ["delta"], "minimum": -100, "maximum": 100}),
        ),
        resource(
            "right_encoder",
            "encoder.right",
            "encoder",
            "int",
            false,
            true,
            json!({"operations": ["delta"], "minimum": -100, "maximum": 100}),
        ),
        resource(
            "buttplug_state",
            "buttplug.state",
            "sensor",
            "object",
            true,
            false,
            json!({}),
        ),
        resource(
            "framebuffer",
            "display.framebuffer",
            "display",
            "rgb565",
            true,
            false,
            json!({"width": 320, "height": 240, "encoding": "rgb565-rle"}),
        ),
    ]
}

fn resource(
    id: &str,
    path: &str,
    category: &str,
    kind: &str,
    readable: bool,
    writable: bool,
    constraints: Value,
) -> Value {
    json!({
        "id": id,
        "path": path,
        "category": category,
        "type": kind,
        "units": null,
        "readable": readable,
        "writable": writable,
        "streamable": false,
        "persistent": false,
        "leaseRequired": writable,
        "safetyCritical": false,
        "available": true,
        "constraints": constraints,
    })
}

fn completed(id: u64, result: Value) -> Vec<u8> {
    serde_json::to_vec(&json!({
        "v": 1,
        "id": id,
        "stage": "completed",
        "ok": true,
        "code": "ok",
        "result": result,
    }))
    .expect("RAD BLE success response must serialize")
}

fn failed(id: u64, code: &str, message: impl Into<String>) -> Vec<u8> {
    serde_json::to_vec(&json!({
        "v": 1,
        "id": id,
        "stage": "failed",
        "ok": false,
        "code": code,
        "message": message.into(),
    }))
    .expect("RAD BLE failure response must serialize")
}

fn dispatch_request(
    bytes: &[u8],
    controls: &mpsc::Sender<RadBleControlEvent>,
    state: &SharedRadBleState,
) -> Vec<u8> {
    let request: Value = match serde_json::from_slice(bytes) {
        Ok(request) => request,
        Err(error) => return failed(0, "invalid_json", error.to_string()),
    };
    let id = request.get("id").and_then(Value::as_u64).unwrap_or(0);
    if request.get("v").and_then(Value::as_u64) != Some(1) {
        return failed(id, "unsupported_version", "RAD BLE v1 is required");
    }
    let Some(operation) = request.get("op").and_then(Value::as_str) else {
        return failed(id, "invalid_request", "op is required");
    };
    log::info!("RAD_BLE_REQUEST {}", String::from_utf8_lossy(bytes));

    let response = match operation {
        "control.acquire" => {
            with_state(state, |state| state.lease_active = true);
            completed(id, json!({"lease": LEASE_ID, "ttlMs": 30000}))
        }
        "control.renew" => {
            if valid_lease(&request, state) {
                completed(id, json!({"lease": LEASE_ID, "ttlMs": 30000}))
            } else {
                failed(
                    id,
                    "lease_required",
                    "acquire the RAD BLE control lease first",
                )
            }
        }
        "control.release" => {
            with_state(state, |state| state.lease_active = false);
            completed(id, json!({"released": true}))
        }
        "catalog.list" | "catalog.read" => catalog_response(id, &request),
        "input.emit" => input_response(id, &request, controls, state),
        "encoder.delta" => encoder_response(id, &request, controls, state),
        "state.read" => completed(
            id,
            if request_wants_summary(&request) {
                runtime_state_summary_json(state)
            } else {
                runtime_state_json(state)
            },
        ),
        "sensor.read" if request.get("path").and_then(Value::as_str) == Some("buttplug.state") => {
            completed(
                id,
                if request_wants_summary(&request) {
                    runtime_state_summary_json(state)
                } else {
                    runtime_state_json(state)
                },
            )
        }
        "output.read"
            if request.get("path").and_then(Value::as_str) == Some("display.framebuffer") =>
        {
            framebuffer_response(id, &request, state)
        }
        "test.reset" => match controls.try_send(RadBleControlEvent::Reset) {
            Ok(()) => completed(id, json!({"queued": true})),
            Err(error) => failed(id, "busy", format!("could not queue reset: {error}")),
        },
        "test.reboot" => match schedule_restart() {
            Ok(()) => completed(id, json!({"queued": true, "delayMs": 150})),
            Err(error) => failed(id, "busy", error),
        },
        _ => failed(
            id,
            "unsupported_operation",
            format!("{operation} is not implemented by this Rust probe"),
        ),
    };
    log::info!("RAD_BLE_RESPONSE {}", String::from_utf8_lossy(&response));
    response
}

fn schedule_restart() -> Result<(), String> {
    thread::Builder::new()
        .name("matrix-restart".to_owned())
        .stack_size(4 * 1024)
        .spawn(|| {
            // Preserve the terminal GATT indication before restarting into a
            // fresh upstream server/device map for the next physical fixture.
            thread::sleep(Duration::from_millis(150));
            unsafe { esp_idf_svc::sys::esp_restart() }
        })
        .map(|_| ())
        .map_err(|error| format!("could not schedule matrix restart: {error}"))
}

fn framebuffer_response(id: u64, request: &Value, state: &SharedRadBleState) -> Vec<u8> {
    const MAX_CHUNK_BYTES: usize = 180;
    let arguments = request.get("args").unwrap_or(&Value::Null);
    let offset = arguments.get("offset").and_then(Value::as_u64).unwrap_or(0) as usize;
    let requested = arguments
        .get("length")
        .and_then(Value::as_u64)
        .unwrap_or(MAX_CHUNK_BYTES as u64) as usize;
    let state = read_state(state);
    let total = state.framebuffer_rle.len();
    let start = offset.min(total);
    let end = start
        .saturating_add(requested.min(MAX_CHUNK_BYTES))
        .min(total);
    completed(
        id,
        json!({
            "width": 320,
            "height": 240,
            "format": "rgb565-le",
            "encoding": "rgb565-rle-u16le",
            "rawBytes": 153600,
            "generation": state.framebuffer_generation,
            "hash": format!("{:08x}", state.framebuffer_hash),
            "offset": start,
            "total": total,
            "data": BASE64.encode(&state.framebuffer_rle[start..end]),
            "eof": end == total,
            "available": total > 0,
        }),
    )
}

fn valid_lease(request: &Value, state: &SharedRadBleState) -> bool {
    request.get("lease").and_then(Value::as_u64) == Some(u64::from(LEASE_ID))
        && read_state(state).lease_active
}

fn catalog_response(id: u64, request: &Value) -> Vec<u8> {
    let resources = resources();
    let total = resources.len();
    let arguments = request.get("args").unwrap_or(&Value::Null);
    let page = arguments.get("page").and_then(Value::as_u64).unwrap_or(0) as usize;
    let page_size = arguments
        .get("pageSize")
        .and_then(Value::as_u64)
        .unwrap_or(1)
        .clamp(1, 1) as usize;
    let pages = total.div_ceil(page_size);
    let page_resources = resources
        .into_iter()
        .skip(page.saturating_mul(page_size))
        .take(page_size)
        .collect::<Vec<_>>();
    completed(
        id,
        json!({
            "page": page,
            "pageSize": page_size,
            "total": total,
            "pages": pages,
            "resources": page_resources,
        }),
    )
}

fn input_response(
    id: u64,
    request: &Value,
    controls: &mpsc::Sender<RadBleControlEvent>,
    state: &SharedRadBleState,
) -> Vec<u8> {
    if !valid_lease(request, state) {
        return failed(
            id,
            "lease_required",
            "acquire the RAD BLE control lease first",
        );
    }
    let path = request
        .get("path")
        .and_then(Value::as_str)
        .unwrap_or_default();
    let event = request
        .get("args")
        .and_then(|arguments| arguments.get("event"))
        .and_then(Value::as_str)
        .unwrap_or("click");
    if event != "click" {
        return failed(
            id,
            "invalid_value",
            "this probe accepts only button click events",
        );
    }
    let control = match path {
        "button.left" => RadBleControlEvent::Left,
        "button.middle" => RadBleControlEvent::Middle {
            defer_ms: request
                .get("args")
                .and_then(|arguments| arguments.get("deferMs"))
                .and_then(Value::as_u64)
                .unwrap_or(0)
                .min(10_000) as u32,
        },
        "button.right" => RadBleControlEvent::Right,
        _ => return failed(id, "unknown_path", format!("unknown button path {path}")),
    };
    match controls.try_send(control) {
        Ok(()) => completed(id, json!({"path": path, "event": event, "queued": true})),
        Err(error) => failed(id, "busy", format!("could not queue button event: {error}")),
    }
}

fn encoder_response(
    id: u64,
    request: &Value,
    controls: &mpsc::Sender<RadBleControlEvent>,
    state: &SharedRadBleState,
) -> Vec<u8> {
    if !valid_lease(request, state) {
        return failed(
            id,
            "lease_required",
            "acquire the RAD BLE control lease first",
        );
    }
    let path = request
        .get("path")
        .and_then(Value::as_str)
        .unwrap_or_default();
    let Some(delta) = request
        .get("args")
        .and_then(|arguments| arguments.get("delta"))
        .and_then(Value::as_i64)
        .and_then(|value| i32::try_from(value).ok())
        .filter(|value| (-100..=100).contains(value) && *value != 0)
    else {
        return failed(
            id,
            "invalid_value",
            "encoder delta must be -100..-1 or 1..100",
        );
    };
    let event = match path {
        "encoder.left" => RadBleControlEvent::EncoderLeft { delta },
        "encoder.right" => RadBleControlEvent::EncoderRight { delta },
        _ => return failed(id, "unknown_path", format!("unknown encoder path {path}")),
    };
    match controls.try_send(event) {
        Ok(()) => completed(id, json!({"path": path, "delta": delta, "queued": true})),
        Err(error) => failed(
            id,
            "busy",
            format!("could not queue encoder delta: {error}"),
        ),
    }
}

fn install_request_handler(
    characteristic: &Arc<NimbleMutex<BLECharacteristic>>,
    response: &Arc<NimbleMutex<BLECharacteristic>>,
    controls: mpsc::Sender<RadBleControlEvent>,
    state: SharedRadBleState,
) {
    let response = response.clone();
    characteristic.lock().on_write(move |arguments| {
        let response_bytes = dispatch_request(arguments.recv_data(), &controls, &state);
        response.lock().set_value(&response_bytes).notify();
    });
}

pub fn install(
    ble_device: &BLEDevice,
    controls: mpsc::Sender<RadBleControlEvent>,
    state: SharedRadBleState,
) -> Result<(), String> {
    let advertising = ble_device.get_advertising();
    let server = ble_device.get_server();
    server.on_connect(|server, descriptor| {
        log::info!("RAD BLE central connected: {descriptor:?}");
        if let Err(error) = server.update_conn_params(descriptor.conn_handle(), 12, 24, 0, 200) {
            log::warn!("Could not update RAD BLE connection parameters: {error:?}");
        }
    });
    server.on_disconnect(|descriptor, reason| {
        log::info!("RAD BLE central disconnected: {descriptor:?} ({reason:?})");
        // esp32-nimble's built-in reconnect advertising is compiled out for
        // some extended-advertising ESP-IDF configurations. Restart here as
        // well so a released matrix-control connection is always discoverable.
        if let Err(error) = BLEDevice::take().get_advertising().lock().start() {
            log::warn!("Could not restart RAD BLE advertising after disconnect: {error:?}");
        }
    });

    let service = server.create_service(uuid128!("522b443a-5241-4452-0001-420badbabe69"));
    let protocol_info = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-0002-420badbabe69"),
        NimbleProperties::READ,
    );
    protocol_info.lock().set_value(
        serde_json::to_string(&json!({
            "protocol": "rad-ble",
            "version": 1,
            "deviceType": "RADR",
            "deviceName": "RADR Rust Buttplug",
            "serviceUuid": SERVICE_UUID,
            "firmwareVersion": env!("CARGO_PKG_VERSION"),
            "build": "upstream-buttplug-rust",
            "maxMtu": 512,
            "maxMessageBytes": 509,
            "security": "open",
            "directOta": false,
            "directFilesystemOta": false,
            "capabilities": ["catalog", "input", "encoder", "buttplug", "display.framebuffer"],
            "capabilityHash": "radr-upstream-buttplug-v1",
            "stateHeartbeatMs": 0,
            "essentialState": null,
            "otaResumeTtlMs": 0,
            "streamHeaderBytes": 20,
        }))
        .expect("RAD BLE protocol info must serialize")
        .as_bytes(),
    );

    let catalog = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-0003-420badbabe69"),
        NimbleProperties::READ,
    );
    catalog
        .lock()
        .set_value(br#"{"v":1,"total":7,"pageSize":1}"#);
    let device_name = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-0004-420badbabe69"),
        NimbleProperties::READ,
    );
    device_name.lock().set_value(b"RADR Rust Buttplug");
    let identity = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-0005-420badbabe69"),
        NimbleProperties::READ,
    );
    identity.lock().set_value(
        br#"{"deviceType":"RADR","implementation":"rust","buttplugSource":"official-upstream"}"#,
    );

    let response = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-1100-420badbabe69"),
        // Responses are request/response control traffic, so use acknowledged
        // GATT indications rather than lossy notifications. This matters for
        // deterministic multi-chunk framebuffer and matrix reads.
        NimbleProperties::READ | NimbleProperties::INDICATE,
    );
    response
        .lock()
        .set_value(br#"{"v":1,"id":0,"stage":"completed","ok":true,"code":"ready"}"#);
    let request = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-1000-420badbabe69"),
        NimbleProperties::WRITE | NimbleProperties::WRITE_NO_RSP,
    );
    install_request_handler(&request, &response, controls.clone(), state.clone());
    let button = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-3000-420badbabe69"),
        NimbleProperties::WRITE | NimbleProperties::WRITE_NO_RSP,
    );
    install_request_handler(&button, &response, controls.clone(), state.clone());
    let encoder = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-3010-420badbabe69"),
        NimbleProperties::WRITE | NimbleProperties::WRITE_NO_RSP,
    );
    install_request_handler(&encoder, &response, controls, state.clone());

    let state_characteristic = service.lock().create_characteristic(
        uuid128!("522b443a-5241-4452-2000-420badbabe69"),
        NimbleProperties::READ | NimbleProperties::NOTIFY,
    );
    let state_for_read = state.clone();
    state_characteristic
        .lock()
        .on_read(move |characteristic, _| {
            let bytes = serde_json::to_vec(&runtime_state_json(&state_for_read))
                .expect("RAD BLE state must serialize");
            characteristic.set_value(&bytes);
        });

    let mut advertisement = BLEAdvertisementData::new();
    advertisement
        .name("RADR Rust Buttplug")
        .add_service_uuid(uuid128!("522b443a-5241-4452-0001-420badbabe69"));
    advertising
        .lock()
        .set_data(&mut advertisement)
        .map_err(|error| format!("could not configure RAD BLE advertising: {error:?}"))?;
    advertising
        .lock()
        .start()
        .map_err(|error| format!("could not start RAD BLE advertising: {error:?}"))?;
    with_state(&state, |state| state.phase = "scanning".to_owned());
    log::info!("Rust RAD BLE v1 advertising as RADR Rust Buttplug");
    Ok(())
}
