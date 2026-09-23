# Development display capture

Development profiles `development` enable the
read-only `screen` command on the normal 115200-baud USB serial port. Production
and staging profiles omit the console, its task and its capture memory.
Existing development aliases retain their behavior.

Build explicitly without uploading:

```sh
pio run -d Software -e development -t buildprog
```

Use the enrolled device and its matching flash/PSRAM profile. Do not replace its
partition layout or erase NVS to take a screenshot. Close other serial monitors
and wait for a hardware-validation job to finish before opening its port. Keep
the device idle (and a Lockbox unlocked).

```sh
python -m pip install pyserial
python scripts/capture_screen.py --port COM_PORT --product RADR --output-dir captures --name current
```

Replace COM_PORT with the verified device port. Opening serial can reset some
boards; use `--startup-delay 10` if needed. Omit `--name` to keep one connection
open while navigating normally; type `capture menu` or `quit`. This works on
Windows and with piped commands. A single capture times out after 90 seconds.

The tool saves a PNG, little-endian RGB565 buffer, and JSON dimensions/checksums.
Rows carry a snapshot ID and RLE pixels; the complete frame must match the
device's FNV-1a checksum. Missing, stale, duplicate or corrupted rows are rejected
without saving a new image. Unrelated device logs are ignored and never saved.
If another task interrupts serial output, issue a new capture after the error.

TFT firmware mirrors drawing operations, including fast bitmap writes, in PSRAM.
OLED firmware copies its existing 1024-byte U8g2 buffer and translates its configured
rotation into the logical UI view. The display mutex is held only while copying;
serial transmission does not pause rendering. This captures the current pixel
buffer, not backlight brightness, panel faults, or an optical photograph. It does
not change product control protocols, pairing, calibration or saved credentials.

Do not commit captures: they may show device or account information. PR checks
compile every development variant and test rejection of damaged capture streams;
staging hardware evidence still uses the exact PR staging artifacts.
