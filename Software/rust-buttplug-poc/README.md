# RADR upstream Buttplug Rust firmware

## Result

The official Buttplug Rust implementation can run directly on the RADR's
ESP32-S3. This target boots a Rust/ESP-IDF firmware, loads Buttplug's complete
embedded device configuration, runs the official server and protocol code in
process, and supplies an ESP32 NimBLE implementation of Buttplug's generic
Bluetooth LE hardware interfaces.

This is the practical equivalent of installing Buttplug as a firmware package:
the upstream crates are linked into one ESP-IDF application image, and the
bundle builder packages that app with its bootloader and partition table in one
factory image. An ELF is also produced, but it is a linker/debug artifact rather
than a second program that the existing Arduino firmware can launch. The ESP32
has no process loader or desktop operating system, so a sidecar ELF is not a
useful deployment model.

The firmware is intentionally separate from the production Arduino target. It
now owns the RADR display, three under-screen buttons, and both rotary encoders.
It also exposes those controls and a lossless copy of the complete framebuffer
over RAD BLE for Motion Lab automation. It does not include the production
updater or the unrelated Arduino device-control implementations.

Both rotary encoders use the ESP32-S3 pulse-counter peripherals for quadrature
edge capture. Full-screen SPI refreshes can delay the Rust event loop without
losing knob movement, and physical knob input remains available while a RAD BLE
control lease is active.

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

## RADR flow

1. Run one unrestricted active BLE scan. Every advertisement is matched against
   all 132 factory-backed BLE protocol IDs in the loaded upstream catalog; RADR
   never chooses a protocol before discovery.
2. Use upstream configuration to discard unsupported advertisements and report
   every matching device, including all candidate protocol names when an
   advertisement is ambiguous. Candidate delivery to the UI is lossless rather
   than capped to a single device or protocol.
3. Show every candidate on the RADR display and in structured RAD BLE state.
   The left/right under-screen buttons or either encoder change the selected
   candidate; the center button explicitly approves it.
4. Only then let the official Buttplug server connect, identify the precise
   device, and initialize its protocol.
5. Enumerate the upstream device features and allowed ranges. Every modifiable
   output is presented as a horizontal 0–100% control bar without maintaining a
   second device/settings schema.
6. In the connected screen, use the right encoder or left/right buttons to
   select a setting, the left encoder to change its value, and the center button
   to send the official upstream all-stop command and zero every bar.
7. Send an upstream-generated all-stop immediately after connection so a newly
   identified device always begins in a safe state.

Each enumerated feature is also emitted on serial as a compact
`BUTTPLUG_FEATURE_JSON` object. Its `definition` is the official v4
`DeviceFeature` wire representation, including modifiable output types and
ranges plus readable or subscribable input capabilities. RADR does not maintain
a second settings schema.

Motion Lab can drive the exact same state machine through the RAD BLE `button`
and `encoder` characteristics. Its `state.read` response includes candidates,
the accepted device, every enumerated output control, the selected control, and
its current percentage. The `display.framebuffer` resource transfers a verified
320×240 RGB565 framebuffer, so automated tests can inspect the complete screen
without a camera.

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
by this firmware without changing the transport or protocols.

## Physical verification

The release firmware was built and flashed to a RADR ESP32-S3 with 16 MB flash
and 8 MB octal PSRAM. The application occupies 4,978,688 of 6,553,600 bytes in
one OTA slot (75.97%). The panel uses `LandscapeInverted(true)`, retaining the
requested horizontal mirror and applying the subsequent 180-degree rotation.

Both physical encoders were exercised on the connected-control screen. The
right encoder selected each upstream-derived setting, while slow and fast left
encoder turns produced single- and multi-detent changes in five-percent steps;
the resulting values were independently visible in structured RAD BLE state.

Motion Lab generated 119 BLE emulator profiles from all 120 active tests in the
unchanged upstream fixture suite at revision
`9571b3db42ee2d7b3342ab9d40eb5c9e45679444`; the sole excluded fixture is
non-BLE TCode. A Rust fixture-radio image was flashed to a Lockbox ESP32-S3 and
the complete catalog was exercised against the physical RADR. The final report
records:

- 119 of 119 fixture profiles passed across 82 unique upstream protocols
- 192 of 192 enumerated settings were selected independently, changed through
  RADR's encoder path, and observed as physical GATT writes
- every setting returned to zero after the center-button all-stop
- all initialization transcripts completed, and every profile with an upstream
  stop assertion emitted a physical stop write
- 108 fresh-connection stops matched the fixture's exact canonical bytes; three
  stateful fixtures used different prior-output or sequence-counter state, so
  validation required the physical initial stop, every control write, the
  physical final stop, and zeroed RADR state instead of assuming static bytes

The report is
`/tmp/radr-buttplug-emulator-matrix-full-rust-all-controls-v9.json`, SHA-256
`e8afb9d154ab620b5574aab351b8cc807daa45c633a4224323d7074d8837e3cf`.
The active upstream fixture suite covers 82 of the 132 factory-backed BLE
protocol IDs. The other 50 remain discoverable from the official configuration
and factory map, but upstream does not currently provide active fixture
transcripts for them; this project does not invent replacement protocol logic.

One unrestricted scan also discovered two emulators concurrently and retained
both candidates: `F1s` (`lelo-f1s`) and `SS-TD-YDTD-001` (`activejoy`). RADR
displayed both, accepted an explicit selection of `F1s`, let the official server
identify `Lelo F1s`, and rendered both upstream-derived controls. Setting both
bars to 25% produced the packets `[1, 25, 0]` and `[1, 25, 25]`; all-stop
produced `[1, 0, 0]`. Motion Lab downloaded and verified the complete 320×240
RGB565 framebuffer without a camera.

## Embedded constraints discovered

- PSRAM is required for the complete catalog and Rust-owned application data.
  A capability-aware global allocator keeps that data in PSRAM and preserves
  internal RAM for NimBLE, FreeRTOS, and DMA-capable peripherals.
- The main task needs a large stack while Buttplug compiles its embedded JSON
  configuration and schemas. The firmware reserves 96 KiB.
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
espflash flash --monitor --partition-table partitions.csv \
  --port /dev/cu.usbserial-0001 \
  target/xtensa-esp32s3-espidf/release/radr-buttplug-poc
```

## Installable bundle

Build the firmware and a validated installation package with one command:

```sh
./tools/build_install_bundle.sh
```

The script uses the project toolchain, builds with the lockfile, and writes these
ignored artifacts under `dist/`:

- `radr-buttplug-factory.bin`: one compact image containing the ESP32-S3
  bootloader at `0x0`, the exact dual-OTA partition table at `0x8000`, and the
  Rust application at `0x10000`
- `radr-buttplug-app.bin`: the same application in standalone ESP-IDF image
  form for partition-aware or future OTA tooling
- `manifest.json` and `install.html`: an ESP Web Tools package for an HTTPS
  host
- `bundle.json`: exact target, source-revision, partition, size, and hash
  metadata
- `SHA256SUMS`: hashes for every file in the package

The builder rejects a bundle unless both images identify as ESP32-S3 DIO/80 MHz
16 MB images, the binary partition table exactly matches `partitions.csv`, the
application fits in `app0`, and the application bytes in both artifacts are
identical. It also requires blank NVS and OTA-selection sectors. The current
factory image is 5,044,224 bytes; it packages the 4,978,688-byte application
without padding the unused remainder of flash.

The factory image can be installed directly at offset zero. This replaces the
bootloader, partition table, NVS, OTA selection data, and active application, so
select the intended RADR serial port explicitly:

```sh
espflash write-bin --chip esp32s3 --port /dev/cu.usbserial-0001 \
  0x0 dist/radr-buttplug-factory.bin
```

For browser installation, host the complete `dist/` directory on one HTTPS
origin and open `install.html`. Pull requests that touch this target independently
rebuild, validate, checksum, and upload the same directory as the
`radr-buttplug-install-bundle` CI artifact.

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
