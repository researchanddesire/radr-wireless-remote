# PR firmware and shared bench validation

Every pull request into `staging` or `main`, including documentation-only edits,
builds the GitHub PR merge commit. `RELEASE_BUILD_SHA` is `github.sha`, never the
PR head SHA. Production profiles compile; only staging artifacts reach fixtures.

| Repositories | Staging and production suffixes | Physical coverage |
| --- | --- | --- |
| Lockbox, Lockbox-OSS | r2, r8 | Both 16 MB ESP32-S3 boards, verified R2/R8 PSRAM |
| DT_Trainer, DT_Trainer-OSS | v1, v2 | 4 MB and 16 MB profiles; actual chip capacities recorded separately |
| OSSM | 4mb, 16mb | One ESP32, selected by measured capacity |
| radr-wireless-remote | existing staging/production aliases | Enrolled 16 MB R8 remote, application and LittleFS |

OSSM retains its original development/staging/production aliases. Its restored
Software tree comes from this repository's main firmware; staging hardware and
assembly documentation remain separate. The public Trainer keeps BLE disabled,
its SDK security patches, both deployed partition tables, and 16 KiB OTA reserve.

OSSM's 4 MB profiles use the standard 4 MB min_spiffs layout and size optimization;
16 MB profiles and the existing aliases retain main's custom `min_spiffs.csv`.
Measured capacity alone does not authorize a partition migration. A 16 MB fixture
still carrying the older 4 MB table fails preflight until a separately reviewed,
storage-preserving migration has been completed. Never relabel it as a 4 MB board.

## Interfaces and trust boundary

`catalog.py` is the reviewed policy for products, physical variants, partition
layouts and writable regions. `bundle.py` emits schema 2 `manifest.json` plus
per-variant images. It records repository, merge SHA, run ID/attempt, product,
environment, chip, flash bytes, PSRAM, offsets, byte lengths, SHA-256 checksums,
and the application digest reported at boot. All variants must be present.
Bootloader, partition table, boot-selection image, application, ELF, and RADR's
LittleFS are included. ELF is retained for debugging and never executed/flashed.

Bundles are **data only**. The Windows worker never checks out PR source or runs
downloaded Python, PowerShell, or other artifact files. It calls the installed
`C:\rad-hil\harness\launch.py` with isolated Python. The host's catalogue and
validator, not a manifest supplied by a PR, decide writable offsets and limits.
The manifest must match the caller's repository, merge SHA, run ID and attempt.
Artifact names include the attempt. Use **Re-run all jobs** to create a fresh
bundle; rerunning only a failed hardware job must not reuse an older attempt's images.

The fixture is schema 1 JSON, stored only at `C:\rad-hil\fixture.json`. Its
`products` object has `lockbox` (r2/r8), `dtt` (v1/v2), `ossm` (exactly one of
4mb/16mb), and `radr` (r8). Each device contains:

```json
{
  "product": "dtt",
  "enrolled": false,
  "device_id": "REPLACE_WITH_VERIFIED_FACTORY_MAC",
  "chip": "esp32",
  "flash_bytes": 4194304,
  "psram": null,
  "usb_vid": 4292,
  "usb_pid": 60000,
  "usb_location": "REPLACE_WITH_VERIFIED_USB_LOCATION"
}
```

Supply exactly one of `usb_serial` or `usb_location`; never use a transient COM
number as identity. Set `enrolled` only after physically identifying the product,
reading its factory MAC/chip/flash, and verifying PSRAM where relevant. The owner
has authorized a 16 MB Trainer to exercise the 4 MB V1 image/partition profile.
That V1 fixture must retain `flash_bytes: 16777216` and explicitly set
`allow_larger_flash: true`; boot evidence still verifies the actual 16 MB chip.
This is profile coverage, not evidence of testing a physically 4 MB chip. The
exception is refused for every other product/variant or capacity. Missing, duplicate, ambiguous,
or incompatible fixtures fail before writes. Every write rechecks chip, MAC,
flash and PSRAM and reads the installed partition table. A different table
requires a separately reviewed storage-preserving migration; no automatic erase
or repartition fallback is permitted. Approved writes exclude NVS, calibration,
identity, coredump and the inactive application slot. RADR writes its filesystem
alongside its matching application.

`RAD_HEALTH` serial JSON schema 1 is the health interface; product control/BLE
protocols are unchanged. Boot evidence binds the runtime image digest and build
SHA to product/variant, factory MAC, physical flash and staging track. The normal
menu/state task supplies progress, independently of the heartbeat task. Lockbox
readiness also retains the existing requirement for a live staging MQTT connection. Health
requires an idle application, a usable IPv4 address on `IoT_PHB`, and a successful
certificate-verified HTTPS request to staging. Readiness must arrive within 180
seconds, followed by 180 continuous seconds measured by both host time and device
uptime. A heartbeat gap over 20 seconds, stalled progress, network loss, reset,
panic, watchdog, brownout or serial error fails permanently. There is no crash
retry or restarted observation window. Keep Lockboxes unlocked and all devices
idle before starting. No test issues motion/lock commands.

## Provisioning and evidence

Use the existing BLE provisioning service on products that expose it. Obtain the
bench password from the operator's local secret store, never source/build flags,
workflow inputs, artifacts or command-line logs. BLE-disabled Trainers use saved
Wi-Fi settings or their existing setup portal. Verify saved connectivity before
starting the run. Flashing preserves NVS. Do not enable BLE on the public Trainer
to make provisioning easier.

Firmware artifacts expire after 14 days. Evidence expires after 30 days and is
uploaded on success and failure: redacted serial/flash logs, per-device JSON,
aggregate JSON, JUnit and workflow diagnostics. Raw serial output can contain
credentials and stays off uploaded logs. Download/setup failure also produces a
failing JUnit report. Current-run evidence replaces Trainer `test/report.json`
freshness; do not commit generated reports.

## Installation and runner migration

1. Review and commit the `hardware-run.yml` wrapper first. Callers and the runner
   group use its full immutable commit SHA from `reviewed-workflows.json`.
   Push that commit with the implementation branch before running its PR. This
   removes the bootstrap dependency on merging a new wrapper onto main first.
   Review wrapper updates and explicitly update the organization allowlist;
   changing a PR workflow alone never authorizes new host code. Follow repository
   manual-commit rules unless the owner explicitly authorizes committing/merging.
2. Run `install.ps1` from the reviewed checkout as the installation owner (or a host administrator). It installs
   the harness outside runner work directories, pins dependencies, records source
   hashes and prevents the runner service account from modifying installed code.
   Fixture JSON and provisioning secrets stay outside source and artifacts.
3. Create one organization runner group named `rad-firmware-validation`, with
   selected access to these six repositories, public repositories permitted,
   `restricted_to_workflows=true`, and exactly the six reviewed wrapper paths.
   `rollout.py prepare` creates reviewable REST payloads; it does not activate rules.
   See [GitHub runner group API](https://docs.github.com/en/rest/actions/self-hosted-runner-groups).
4. Drain and stop the existing Lockbox worker before moving its registration to
   the organization. Keep one worker only and retain `lkbx-rig-01`; add `rad-hil`.
   Register it in this group and run the service as the existing dedicated account.
   Never run it as administrator. Grant write access only to runner work/temp and
   lock directories; grant read/execute access to the harness and fixture.
5. Hardware jobs do not cancel in-progress flashing. One worker serializes jobs
   across repositories; OS locks additionally cover shared product families and
   the legacy Lockbox `C:\lkbx-hil\fixture.lock`. Within a job both required
   variants flash and observe in parallel. A killed flash is a failure and needs
   explicit recovery; it never becomes a passing run on retry.
6. Run real PRs against both persistent branches, including a docs-only PR, for
   each product. Check complete device coverage, runtime merge SHA/image digest,
   180-second observation, network continuity, and the Lockbox migration regression.
   Fork builds remain on hosted runners; move reviewed changes to an internal
   branch before hardware validation can pass.
7. Only after those runs pass, activate an additive branch ruleset requiring
   `Build validation` and `Hardware validation`, integration ID 15368 (GitHub
   Actions), with strict up-to-date branches, for main and staging. Preserve all
   existing review/check/release rules. Never bypass failed or skipped devices.

## Verification

Run `python -m unittest discover -s scripts/hil -p 'test_*.py'` from the checkout.
Tests exercise corrupted/stale bundles, wrong repository/chip/flash, overflowing
regions, missing variants/fixtures, cross-process locks, malformed serial records,
boot loops, stalled tasks, heartbeat gaps and network loss. These synthetic tests
do not replace full real PR checks or physical failure-injection verification.
