# RADR — OSSM Wireless Remote

ESP32-S3 firmware for **RADR** ("radar"), an open-source handheld BLE controller for the OSSM and other intimate BLE devices (Lovense, Buttplug.io device list). Open hardware under CERN-OHL-S v2.

Upstream: `researchanddesire/radr-wireless-remote` on GitHub. Current firmware version: see `version.json` (currently 1.0.x).

## Repo Layout

| Path | What's there |
|------|--------------|
| `Software/` | PlatformIO firmware project — this is where code work happens |
| `Software/src/main.cpp` | Entry point — sets up buttons, services, kicks off state machine |
| `Software/src/state/` | `boost::sml` state machine (`machine.h`), events, guards, actions |
| `Software/src/services/` | Hardware service singletons (display, encoder, IMU, LEDs, battery, BLE coms, vibrator, WiFi manager, sleep/wake) |
| `Software/src/devices/` | BLE device drivers — `researchAndDesire/ossm`, `lovense/`, `buttplugio/`, plus `registry.*` |
| `Software/src/pages/` | UI pages and menu/controller rendering |
| `Software/src/components/` | Reusable display widgets (icons, buttons, dial, graph, text) |
| `Software/src/tasks/` | FreeRTOS task wrappers (e.g. OTA `update`) |
| `Software/src/pins.h` | Pin map — single source of truth for GPIO assignments |
| `Software/src/constants.h` | Build-wide constants |
| `Software/data/` | LittleFS image contents (`registry.json`, `metadata.json`, `protocols/`) |
| `Software/lib/` | Vendored libs not on the registry (e.g. SparkFun LSM6DS3) |
| `Software/boards/` | Custom PlatformIO board JSONs for `esp32-s3-devkitc-1-n16` and `n16r8v` |
| `Software/scripts/` | Shell helpers — `version_bump.sh`, `img_to_c.sh`, `update_docs_site.sh`, etc. |
| `Hardware/` | KiCad/Altium PCB files, schematic PDF, BOM, 3D-print STEP/STL/3MF, photos |
| `Documentation/` | Mintlify docs site (MDX) — published to `docs.researchanddesire.com/radr` |
| `manifest.json` | ESP Web Tools flasher manifest pointing at Supabase-hosted firmware artifacts |
| `version.json` | Single source of truth for firmware version |
| `.github/workflows/` | CI: PR checks, master deploy, alpha/production releases, version bumps, docs dispatch |

## Build & Flash

PlatformIO project. From `Software/`:

```bash
pio run -e development           # build dev firmware
pio run -e development -t upload # flash + auto-monitor (targets line in [env:development])
pio run -e production            # build production firmware
pio run -t uploadfs              # flash LittleFS data partition
```

- Default board: `esp32-s3-devkitc-1-n16r8v` (16 MB flash, 8 MB PSRAM, octal). The non-`r8v` variant exists for early v1.x PCBs that can't use PSRAM.
- Filesystem: LittleFS. Custom partition table at `Software/partition.csv`.
- Framework: Arduino on `espressif32@6.11.0`. C++17 (`-std=gnu++17`).
- Dev build defines `DEBUG`, `MUTE`, `FORCE_UPDATE`, and points `RAD_SERVER` at a LAN dev host. Production points at `dashboard.researchanddesire.com`.
- OTA firmware artifacts live in a Supabase Storage bucket (`radr-firmware`); see `manifest.json` and `UPDATE_SERVER_URL` build flag.

## Key Architectural Notes

- **State machine**: app flow is a `boost::sml` transition table in `state/machine.h`. States cover `device_search → device_list → device_connecting → device_draw_control`, plus `main_menu`, `settings_menu`, `deep_sleep`, etc. Buttons in `main.cpp` dispatch events (`left_shoulder_pressed`, `middle_button_pressed`, …) into `stateMachine->process_event(...)`. Modify the table, not ad-hoc flags, when adding flow.
- **Device registry**: `devices/registry.*` + `Software/data/registry.json` describe known device protocols. New device support generally means: add a driver under `devices/<vendor>/`, register it, and update the LittleFS registry.
- **Buttons**: handled via `OneButton` library; five physical buttons (two shoulder, three under). Any input handler should also call `setNotIdle("<source>")` so the sleep/wake monitor sees activity.
- **Display**: ST7735/ST7789-class color screen via Adafruit GFX. Pages render through `pages/controller.*` and reusable widgets in `components/`.
- **Power/idle**: `services/sleepWakeup`, `services/lastInteraction`, `services/battery` (MAX1704X fuel gauge). Deep sleep is a real state — don't add long-lived heap allocations that break across sleep cycles.

## Conventions

- Pin changes go through `src/pins.h`. Don't hardcode GPIO numbers elsewhere.
- New build-wide tunables go in `src/constants.h` or a build flag in `platformio.ini`.
- New BLE service/characteristic UUIDs go in `devices/serviceUUIDs.h`.
- Version bumps are driven by `scripts/version_bump.sh` and the `version_bump.yml` workflow — don't hand-edit `version.json` for a release.
- Docs live alongside the code: `Documentation/radr/**/*.mdx`. User-facing changes (new device, new menu, new flashing flow) should land a docs update in the same PR.
- Hardware files in `Hardware/` are binary (Altium, STEP, STL, 3MF, PDF, XLSX). Treat them as artifacts — don't try to "edit" them in a PR; replace whole files when the PCB or print revision changes.

## Don'ts

- Don't add another global singleton next to the `services/` pile when an existing service can grow a method — they already share init order assumptions in `main.cpp::setup()`.
- Don't bypass the state machine with bool flags; events + guards exist for a reason.
- Don't commit firmware binaries here — releases publish to the Supabase bucket via CI.
- This repo is public and the product is adult-oriented; keep code comments and commit messages professional.

## When in Doubt

- User-facing behavior questions → check `Documentation/radr/guides/user-guide/*.mdx` first; it describes the intended UX.
- "How does X get on the screen?" → trace from a state in `state/machine.h` to a `drawXxx` action in `state/actions.hpp` to a page in `pages/`.
- "How does the remote talk to device X?" → `devices/registry.*` + the per-vendor folder.
