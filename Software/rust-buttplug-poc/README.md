# RADR upstream Buttplug Rust probe

## Result

The official Buttplug Rust implementation can run directly on the RADR's
ESP32-S3. This probe boots a Rust/ESP-IDF firmware, loads Buttplug's complete
embedded device configuration, runs the official server and protocol code in
process, and supplies an ESP32 NimBLE implementation of Buttplug's generic
Bluetooth LE hardware interfaces.

This is the practical equivalent of installing Buttplug as a firmware package:
the upstream crates are linked into one ESP-IDF application image. An ELF is
also produced, but it is a linker/debug artifact rather than a second program
that the existing Arduino firmware can launch. The ESP32 has no process loader
or desktop operating system, so a sidecar ELF is not a useful deployment model.

The probe is intentionally separate from the production Arduino target. It does
not yet include the RADR display, controls, updater, or motor-control
application, and it never drives the RADR motor output.

## What is upstream and what is local

The five direct Buttplug dependencies are pinned to official upstream revision
`9571b3db42ee2d7b3342ab9d40eb5c9e45679444`, corresponding to Buttplug 11.0.0
and device-config schema 5.30. The following stay in upstream code:

- device names, advertisements, services, and characteristics
- device/protocol matching and initialization
- command encoding and response parsing
- feature enumeration and command ranges
- the Buttplug server, client, messages, and allow/deny behavior

The local `esp32_ble` module provides only the target-specific transport:

- active BLE scan and advertisement assembly
- connection and GATT service/characteristic discovery
- reads and writes, including write-with-response selection
- notification and indication subscription
- idempotent subscriptions and write-mode fallback matching the behavior of
  Buttplug's desktop BLE manager
- disconnect and hardware events

The full upstream catalog is loaded, but this transport makes only its BLE
devices usable. Upstream USB, HID, serial, websocket, and host-specific
communication managers are not present on the ESP32.

At the pinned revision, the official server exports 141 protocol factories and
the catalog contains 138 BLE protocol IDs. Their intersection is 132
factory-backed BLE protocol IDs. Six catalog-only entries (`cueme`,
`kiiroo-v1`, `libo-karen`, `muse`, `sayberx`, and `twerkingbutt`) have no
factory in the official server, so RADR excludes them from its
supported-candidate list just as the upstream server ultimately would. This
filter is derived from the upstream factory map, not a locally maintained
device list.

Those 132 BLE protocols resolve to 1,021 protocol/identifier entries in the
schema, representing 870 unique device-definition IDs and 853 definition
names. These are upstream catalog-coverage counts, not claims that every
physical model has been qualified on RADR hardware.

The only patched transitive crate is the included DashMap 6.2.1 source. On
ESP-IDF, its host-parallelism-based default can select an invalid shard count.
The target-specific patch selects two shards while preserving DashMap's normal
behavior elsewhere. The official Buttplug source is unmodified.

## RADR flow represented by the probe

1. Scan for BLE advertisements.
2. Use upstream configuration to discard unsupported advertisements and report
   candidate protocol names for supported devices.
3. List candidates in the serial log. The left and right under-screen buttons
   change the selected candidate; the center button explicitly approves it.
4. Only then let the official Buttplug server connect, identify the precise
   device, and initialize its protocol.
5. Enumerate the upstream device features and their allowed ranges.
6. Send an upstream-generated all-stop command as the only automatic command.

Each enumerated feature is also emitted on serial as a compact
`BUTTPLUG_FEATURE_JSON` object. Its `definition` is the official v4
`DeviceFeature` wire representation, including modifiable output types and
ranges plus readable or subscribable input capabilities. RADR does not maintain
a second settings schema.

For the physically verified XHT profile, the upstream feature serializes as:

```json
{"definition":{"FeatureDescription":"","FeatureIndex":0,"Output":{"Vibrate":{"Value":[0,68]}}},"device":"Mizz Zee Device","feature_index":0}
```

The approval gate is intentionally outside the stock server. A nearby
`NBScooter0282` was classified as a possible `lovense` device because it shared
the broad Nordic UART advertisement used by that upstream matcher. GATT
specialization rejected it, but the observation demonstrates why a match must
be presented to the user instead of being treated as permission to connect.
Buttplug's address allow/deny entries can provide an additional persistent
policy. A production RADR screen can consume the same candidate channel used
by this serial/button probe without changing the transport or protocols.

## Physical verification

This was built and flashed to a RADR ESP32-S3 with 16 MB flash and 8 MB octal
PSRAM. A CoreBluetooth test peripheral advertised the real upstream `XHT`
profile (`mizzzee-v2`, service `0xEEA0`, writable characteristic `0xEE01`). The
RADR then:

- matched `mizzzee-v2` from the upstream catalog
- connected and completed GATT specialization
- added `Mizz Zee Device` through the official server
- exposed feature 0 as `Vibrate` with 69 discrete levels (`0..=68`)
- sent all-stop successfully
- wrote the exact upstream-encoded packet `69 96 04 02 00 2c 00`

Those physical results cover the base probe through commit `32806a9`. Later
factory filtering, structured settings output, read/notification profiles, and
transport lifecycle hardening are release-build verified but await a fresh
hardware run.

The current release application occupies 4,841,968 of 6,553,600 bytes in one
OTA slot (73.88%). On the physically tested predecessor image, after approved
connection and command transmission, the measured free heaps were 139,820
bytes internal and 6,303,852 bytes external. The main task retained 45,668
bytes of stack headroom; the BLE worker retained 12,952 bytes.

The nearby device advertising as `S57 D17E LE` was observed by the ESP32, but
Buttplug schema 5.30 has no matching name, service `0xFE07`, or manufacturer
identifier 1447. The probe correctly filters it as unsupported. This is a
catalog limitation, not a transport failure; it would need an upstream device
configuration/protocol contribution or a local user configuration.

## Embedded constraints discovered

- PSRAM is required for the complete catalog and Rust-owned application data.
  A capability-aware global allocator keeps that data in PSRAM and preserves
  internal RAM for NimBLE, FreeRTOS, and DMA-capable peripherals.
- The main task needs a large stack while Buttplug compiles its embedded JSON
  configuration and schemas. The probe reserves 96 KiB.
- Fat LTO deterministically miscompiled an aggregate returned while creating a
  connected upstream device with the tested Xtensa toolchain. Release LTO is
  disabled; size optimization remains enabled.
- The flash image must use DIO mode on this board. The partition table mirrors
  the production dual-OTA 16 MB layout.
- Only manufacturer identifiers referenced by upstream configuration are kept
  in the scan cache. This prevents rotating unrelated advertisements from
  growing memory indefinitely while preserving all configured matches.
- `esp32-nimble` gives NimBLE a raw pointer to each client object. Clients are
  boxed before connection so their addresses remain stable, and disconnected
  clients receive a short retirement grace period before deallocation. Remote
  peripheral termination was tested after this lifecycle fix without a panic.
- BLE session ownership moves from the GATT specializer to the finished
  hardware object. Either owner queues a disconnect when dropped, so a broad
  advertisement match that fails GATT or protocol initialization does not leak
  a client connection.

## Updating upstream support

To adopt a newer Buttplug catalog and protocol set, update all five Git
revisions in `Cargo.toml` and `UPSTREAM_BUTTPLUG_REVISION` in `src/main.rs`
together, regenerate `Cargo.lock`, then rebuild and rerun the BLE hardware test.
This is a firmware update, not a runtime package download. Device behavior
still arrives from upstream without adding one-off protocol implementations to
RADR.

For production, the lowest-risk architecture is to evolve this into the RADR
application firmware and port the existing UI, update, storage, and actuator
layers around it. A C ABI/static-library bridge into the existing Arduino
binary is theoretically possible, but it would still create one combined image
and would add allocator, executor, build-system, and ownership boundaries
without avoiding the Rust/ESP-IDF runtime requirements.

## Toolchain and build

The tested host setup uses `rustup` 1.29.1, `espup` 0.17.1, Espressif Rust 1.97,
ESP-IDF 5.5.3, `ldproxy` 0.3.5, and `espflash` 4.5.0. On macOS with Homebrew:

```sh
brew install rustup
export PATH="/opt/homebrew/opt/rustup/bin:$HOME/.cargo/bin:$PATH"
cargo install espup ldproxy espflash
espup install
```

Build from this directory:

```sh
. "$HOME/.cargo/esp-export.sh"
export PATH="/opt/homebrew/opt/rustup/bin:$HOME/.cargo/bin:$PATH"
cargo build --release --locked
```

The resulting ELF is at
`target/xtensa-esp32s3-espidf/release/radr-buttplug-poc`. `espflash` converts it
to the ESP-IDF boot/application image while flashing. Select the intended RADR
serial port explicitly:

```sh
export PATH="$HOME/.cargo/bin:$PATH"
espflash flash --monitor --port /dev/cu.usbserial-0001 \
  target/xtensa-esp32s3-espidf/release/radr-buttplug-poc
```

## Reproducing the BLE protocol test

On a macOS host with Bluetooth enabled, compile and start the included mock
peripheral before starting the RADR scan:

```sh
swiftc -framework CoreBluetooth -framework Foundation \
  tools/macos_ble_mock.swift -o /tmp/radr-buttplug-ble-mock
/tmp/radr-buttplug-ble-mock mizzzee-v2
```

The RADR serial log lists and selects `XHT`; press the center under-screen
button to approve the connection. The mock then prints each write it receives,
culminating in the seven-byte all-stop packet shown above.

The same mock has a second profile for the receive path:

```sh
/tmp/radr-buttplug-ble-mock vibcrafter
```

That profile advertises the official `Janna` VibCrafter configuration, exposes
its configured TX/RX characteristics, accepts the upstream encrypted
authentication write, and notifies the upstream protocol with the correctly
encrypted `OK;` response. It deliberately exposes only write-with-response so
the ESP32 transport also exercises the same write-mode fallback provided by
Buttplug's desktop BLE manager. A successful run reaches `VibCrafter Janna`,
enumerates its vibration feature, and logs that the first notification was
forwarded to the official protocol listener.

For unattended transport regression testing only, the exact mock name can be
approved at compile time. This setting is absent from normal builds:

```sh
RADR_BUTTPLUG_AUTO_APPROVE_NAME=XHT cargo build --release --locked
```

Use `Janna` instead of `XHT` when running the VibCrafter receive-path profile.

The `hismith` profile exercises a direct GATT read used by an official protocol
identifier:

```sh
/tmp/radr-buttplug-ble-mock hismith
```

It advertises `HISMITH`, exposes the configured `FFE5` write service and `FF90`
model service, and returns `10 01` from characteristic `FF96`. The unchanged
upstream identifier converts that response to `1001`, selects `Hismith Sex
Machine`, and exposes its `Oscillate` range. Its upstream-generated all-stop
packet is `aa 04 00 04`. Use `HISMITH` as the test-only auto-approval name for
an unattended run.
