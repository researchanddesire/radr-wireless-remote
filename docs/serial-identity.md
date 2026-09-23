# Serial device identity

Every firmware track prints its target and version at boot and answers `info`
or `identity` followed by a newline on the normal USB/UART port at 115200 baud.
For example, `LKBX-N16R2 1.21.59`. The following `RAD_ID` JSON record has schema 1,
product, model, semantic version, track, build SHA and factory MAC (`device_id`).
It separates target_flash_bytes/target_psram_bytes from measured flash_bytes/
psram_bytes. A 4 MB Trainer image on a 16 MB board therefore reports DTT-N4 while
retaining the true physical capacity in its record. PSRAM bytes are the amount
initialized and available to the application. No Wi-Fi, pairing or account data
is exposed. Identity is a discovery hint, never authorization to flash or move.

Open the serial port, then send `\ninfo\n` to terminate any incomplete old
command. Retry once per second while booting; malformed and oversized commands
are ignored and replies are rate-limited to four per second. There is one input
reader for identity and the existing development-only screen commands. Identity
does not wait for a host, change saved settings or control actuators.

Lab reads identity after its existing ROM hardware inspection, verifies the MAC
and measured flash against that inspection, and labels the board with the
reported model/version. Old firmware without this protocol remains usable.
Firmware selection and flash compatibility still require verified hardware.

For TinyUSB CDC builds, the USB product descriptor uses the model name without
changing VID/PID or the serial number. Espressif's hardware USB Serial/JTAG name
is fixed; CP210x UART bridge strings belong to the separate bridge and require
host-side vendor configuration, not ESP firmware. Do not switch USB transports
or rewrite identities merely to change a display name.
