"""Hold one serial connection from final identity checks through every write."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import esptool
from bundle import partition_rows
from catalog import policy, regions, physical_flash_allowed


def verify_connected(esp, spec, device_id, physical_flash_bytes=None):
    if esp.CHIP_NAME.lower().replace('-', '') != spec['chip']:
        raise RuntimeError('connected chip differs from the enrolled target')
    if bytes(esp.read_mac()).hex().upper() != device_id:
        raise RuntimeError('factory MAC changed before writing')
    capacity_code = (esp.flash_id() >> 16) & 0xff
    if capacity_code not in (22, 24) or 1 << capacity_code != (physical_flash_bytes or spec['flash_bytes']):
        raise RuntimeError('physical flash capacity changed before writing')
    if spec['psram']:
        repeat, word4, word5 = (esp.read_reg(0x6000703c + n) for n in (0, 24, 28))
        capacity = (((word5 >> 19) & 1) << 2) | ((word4 >> 3) & 3)
        power = (repeat >> 8) & 1
        variant = 'r2' if power == 0 and capacity in (1, 2) else 'r8' if power == 1 and capacity == 1 else 'unknown'
        if variant != spec['psram']:
            raise RuntimeError('PSRAM identity changed before writing')
    if partition_rows(esp.read_flash(0x8000, 0x1000)) != spec['partitions']:
        raise RuntimeError('installed partitions changed before writing')


def write_verified(port, spec, device_id, bundle, physical_flash_bytes=None, allow_larger_flash=False):
    physical_flash_bytes = physical_flash_bytes or spec['flash_bytes']
    if not physical_flash_allowed(spec, physical_flash_bytes, allow_larger_flash):
        raise RuntimeError('physical flash substitution is not explicitly approved')
    esp = esptool.detect_chip(port=port, baud=115200, connect_attempts=3)
    try:
        esp = esp.run_stub()
        verify_connected(esp, spec, device_id, physical_flash_bytes)
        # Never let esptool reconnect and retry a failed write behind the
        # fixture identity checks. A fresh reviewed run is required instead.
        esp.WRITE_FLASH_ATTEMPTS = 1
        nvs = next(row for row in spec['partitions'] if row[0] == 'nvs')
        saved_nvs = esp.read_flash(nvs[3], nvs[4])
        # The connected loader is already a stub. Without --no-stub esptool's
        # CLI uploads another copy over the resident helper before any write.
        args = ['--chip', spec['chip'], '--port', port, '--baud', '460800', '--no-stub', '--after', 'no_reset',
                'write_flash', '--compress', '--flash_mode', 'keep', '--flash_freq', 'keep', '--flash_size', 'keep']
        for filename, (offset, _) in regions(spec).items():
            if offset is not None:
                args.extend([hex(offset), str(bundle/filename)])
        # esptool 4.11's esp argument reuses this already-connected loader.
        # There is no reopen/reset gap in which a different USB device can enter.
        esptool.main(args, esp=esp)
        if esp.read_flash(nvs[3], nvs[4]) != saved_nvs:
            raise RuntimeError('NVS changed while flashing; fixture requires recovery')
        print('NVS preservation verified before application restart.', flush=True)
    finally:
        esp._port.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('port', 'repository', 'variant', 'device-id'):
        parser.add_argument('--'+name, required=True)
    parser.add_argument('--bundle', required=True, type=Path)
    parser.add_argument('--physical-flash-bytes', required=True, type=int)
    parser.add_argument('--allow-larger-flash', action='store_true')
    args = parser.parse_args()
    write_verified(args.port, policy(args.repository, args.variant), args.device_id, args.bundle,
                   args.physical_flash_bytes, args.allow_larger_flash)
