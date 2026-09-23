"""Installed harness: verify allowlisted devices before writing any flash."""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager, ExitStack
from datetime import datetime, timezone
import json
import ipaddress
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

from bundle import validate, partition_rows
from catalog import PRODUCTS, REPOSITORIES, policy, regions, physical_flash_allowed
from monitor import Monitor, PREFIX


@contextmanager
def rig_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        stream.seek(0)
        if os.fstat(stream.fileno()).st_size == 0:
            stream.write(b'\0'); stream.flush()
        stream.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError('another process owns this physical fixture') from error
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == 'nt':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def load_fixture(path, repository):
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    if data.get('schema') != 1:
        raise ValueError('unsupported fixture schema')
    product = REPOSITORIES[repository]
    devices = data.get('products', {}).get(product, {})
    expected = set(PRODUCTS[product]['variants'])
    if product == 'ossm':
        if len(devices) != 1 or not set(devices) <= expected:
            raise ValueError('OSSM fixture must name exactly one measured capacity')
    elif set(devices) != expected:
        raise ValueError('fixture does not cover every required variant')
    identities, selectors = set(), set()
    for variant, device in devices.items():
        spec = policy(repository, variant)
        if (not re.fullmatch('[0-9A-F]{12}', device.get('device_id', ''))
                or sum(bool(device.get(k)) for k in ('usb_serial', 'usb_location')) != 1
                or any(type(device.get(k)) is not int for k in ('usb_vid', 'usb_pid'))
                or not physical_flash_allowed(spec, device.get('flash_bytes'), device.get('allow_larger_flash', False))
                or device.get('chip') != spec['chip'] or device.get('product') != product
                or device.get('psram') != spec['psram'] or device.get('enrolled') is not True):
            raise ValueError('unverified or incompatible fixture identity: '+variant)
        selector = (device['usb_vid'], device['usb_pid'], device.get('usb_serial'), device.get('usb_location'))
        if device['device_id'] in identities or selector in selectors:
            raise ValueError('fixture identities must be unique')
        identities.add(device['device_id']); selectors.add(selector)
    return devices


def find_port(device):
    from serial.tools import list_ports
    matches = [p.device for p in list_ports.comports()
               if p.vid == device['usb_vid'] and p.pid == device['usb_pid']
               and (p.serial_number == device['usb_serial'] if device.get('usb_serial')
                    else p.location == device['usb_location'])]
    if len(matches) != 1:
        raise RuntimeError('expected exactly one USB identity match')
    return matches[0]


def esptool(port, spec, args, log, timeout=90):
    command = [sys.executable, '-m', 'esptool', '--chip', spec['chip'], '--port', port,
               '--baud', '460800', '--after', 'no_reset', *args]
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding='utf-8', errors='replace', timeout=timeout)
    log.write(completed.stdout); log.flush()
    if completed.returncode:
        raise RuntimeError('esptool failed: '+args[0])
    return completed.stdout


def probe(port, spec, device, log):
    result = esptool(port, spec, ['flash_id'], log)
    macs = re.findall(r'MAC:\s*([0-9a-fA-F:]{17})', result)
    sizes = re.findall(r'Detected flash size:\s*(\d+)MB', result)
    if not macs or macs[-1].replace(':', '').upper() != device['device_id']:
        raise RuntimeError('factory MAC mismatch; refusing to write')
    if not sizes or int(sizes[-1])*1024*1024 != device['flash_bytes']:
        raise RuntimeError('measured physical flash capacity mismatch')
    with tempfile.TemporaryDirectory(prefix='rad-hil-probe-') as temp:
        temp = Path(temp)
        if spec['psram']:
            path = temp/'efuse.bin'
            esptool(port, spec, ['dump_mem', '0x6000703c', '32', str(path)], log)
            content = path.read_bytes()
            if len(content) != 32:
                raise RuntimeError('invalid eFuse dump')
            repeat, word4, word5 = (struct.unpack_from('<I', content, n)[0] for n in (0, 24, 28))
            capacity = (((word5 >> 19) & 1) << 2) | ((word4 >> 3) & 3)
            power = (repeat >> 8) & 1
            detected = 'r2' if power == 0 and capacity in (1, 2) else 'r8' if power == 1 and capacity == 1 else 'unknown'
            if detected != spec['psram']:
                raise RuntimeError('PSRAM eFuse identity mismatch')
        path = temp/'partitions.bin'
        esptool(port, spec, ['read_flash', '0x8000', '0x1000', str(path)], log)
        # Never overwrite an existing layout implicitly. Enrollment/migration
        # is a separate reviewed operation that preserves persistent storage.
        if partition_rows(path.read_bytes()) != spec['partitions']:
            raise RuntimeError('installed partition layout differs; enrollment/migration required')


def flash(port, spec, device, bundle, log):
    command = [sys.executable, '-I', '-u', str(Path(__file__).with_name('write.py')),
               '--port', port, '--repository', spec['repository'], '--variant', spec['variant'],
               '--device-id', device['device_id'], '--bundle', str(bundle)]
    command += ['--physical-flash-bytes', str(device['flash_bytes'])]
    if device.get('allow_larger_flash') is True:
        command.append('--allow-larger-flash')
    try:
        completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=240)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError('flashing timed out; no retry is permitted') from error
    finally:
        log.flush()
    if completed.returncode:
        raise RuntimeError('verified flash failed; no retry is permitted')


def safe_line(line):
    # Staging Trainer diagnostics contain only fixed application state names.
    states = {'init', 'init.idle', 'init.preflight', 'pairing', 'play',
              'play.bad_health_check', 'play.begin', 'play.calibration.max',
              'play.calibration.min', 'play.details.segment', 'play.details.session',
              'play.details.toy', 'play.failed', 'play.fetchSettings', 'play.final',
              'play.grade', 'play.idle', 'play.loadSettings', 'play.preflight',
              'play.ready', 'play.threestrikes', 'provisioning', 'provisioning.done',
              'provisioning.error', 'provisioning.update_error',
              'provisioning.wifiSettings', 'update', 'wifiSettings'}
    if line.startswith('RAD_STATE ') and line[10:] in states:
        return line
    # Only the structured, credential-free health protocol is retained verbatim.
    # Normal firmware logs may contain request bodies, Wi-Fi keys or pairing codes.
    if PREFIX in line:
        try:
            record = json.loads(line.split(PREFIX, 1)[1])
            patterns = {'event': 'boot|heartbeat', 'product': 'lockbox|dtt|ossm|radr',
                        'variant': 'r2|r8|v1|v2|4mb|16mb', 'build_sha': '[0-9a-f]{40}',
                        'image_sha256': '[0-9a-f]{64}', 'device_id': '[0-9A-F]{12}',
                        'track': 'staging'}
            numeric = {'boot_id', 'uptime_ms', 'schema', 'flash_bytes', 'app_age_ms',
                       'network_age_ms', 'disconnects'}
            boolean = {'ready', 'wifi', 'bench_wifi', 'internet'}
            def safe_value(key, value):
                if key in patterns:
                    return isinstance(value, str) and re.fullmatch(patterns[key], value)
                if key in numeric:
                    return type(value) is int and 0 <= value < 2**64
                if key in boolean:
                    return type(value) is bool
                if key == 'ip' and isinstance(value, str):
                    ipaddress.IPv4Address(value)
                    return True
                return False
            if isinstance(record, dict) and all(safe_value(k, v) for k, v in record.items()):
                return PREFIX+json.dumps(record, separators=(',', ':'))
        except ValueError:
            pass
        return '[malformed or unexpected health record redacted]'
    categories = ['ESP-ROM:', 'rst:0x', 'Guru Meditation', 'panic', 'watchdog', 'Brownout', 'Rebooting']
    hits = [term for term in categories if term.lower() in line.lower()]
    return '[device log redacted'+(': '+', '.join(hits) if hits else '')+']'


def observe(device, port_name, monitor, log):
    import serial
    from esptool.reset import HardReset
    port = serial.Serial(port=None, baudrate=115200, timeout=.25)
    port.dtr = False; port.rts = False; port.port = port_name
    port.open()
    try:
        port.reset_input_buffer()
        HardReset(port, uses_usb=device['usb_vid'] == 0x303a and device['usb_pid'] == 0x1001)()
        pending = bytearray()
        while not monitor.passed:
            monitor.tick(time.monotonic())
            pending.extend(port.read(port.in_waiting or 1))
            if len(pending) > 65536:
                raise RuntimeError('serial line exceeded bounded framing')
            while b'\n' in pending:
                raw, _, rest = pending.partition(b'\n'); pending = bytearray(rest)
                line = raw.decode('utf-8', errors='replace').rstrip('\r')
                now = time.monotonic()
                log.write(f'{now:.3f} {safe_line(line)}\n'); log.flush()
                monitor.feed(line, now)
    finally:
        port.close()


def test_device(variant, device, manifest, bundle, output):
    monitor = None
    result = dict(variant=variant, device_id=device['device_id'], status='failed')
    try:
        spec = policy(manifest['repository'], variant)
        port = find_port(device)
        with (output/(variant+'-flash.log')).open('w', encoding='utf-8') as log:
            probe(port, spec, device, log)
            flash(port, spec, device, bundle/variant, log)
        expected = dict(schema=1, product=spec['product'], variant=variant,
                        device_id=device['device_id'], flash_bytes=device['flash_bytes'],
                        build_sha=manifest['build_sha'], track='staging',
                        image_sha256=manifest['variants'][variant]['image_sha256'])
        monitor = Monitor(expected, time.monotonic())
        with (output/(variant+'-serial.log')).open('w', encoding='utf-8') as log:
            observe(device, port, monitor, log)
        result = monitor.result(time.monotonic())
    except Exception as error:
        if monitor:
            result = monitor.result(time.monotonic())
        result.update(status='failed', error=str(error))
    (output/(variant+'-result.json')).write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f'{variant}: {result["status"]} {result.get("error", "")}', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('repository', 'build-sha', 'run-id', 'run-attempt'):
        parser.add_argument('--'+name, required=True)
    for name in ('bundle', 'fixture', 'output', 'lock-directory'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    try:
        manifest = validate(args.bundle, args.repository, args.build_sha, args.run_id, args.run_attempt)
        devices = load_fixture(args.fixture, args.repository)
        product = manifest['product']
        with ExitStack() as stack:
            stack.enter_context(rig_lock(args.lock_directory/(product+'.lock')))
            # Cooperate with the existing Lockbox runner during migration.
            if product == 'lockbox' and os.name == 'nt':
                stack.enter_context(rig_lock(Path('C:/lkbx-hil/fixture.lock')))
            # Resolve every target before any board is touched.
            ports = [find_port(d) for d in devices.values()]
            if len(set(ports)) != len(ports):
                raise RuntimeError('multiple fixtures resolved to the same port')
            with ThreadPoolExecutor(max_workers=len(devices)) as pool:
                futures = [pool.submit(test_device, v, d, manifest, args.bundle, args.output)
                           for v, d in devices.items()]
                results = [f.result() for f in futures]
    except Exception as error:
        results = [dict(variant='preflight', status='failed', error=str(error))]
    report = dict(schema=2, repository=args.repository, build_sha=args.build_sha,
                  run_id=args.run_id, run_attempt=args.run_attempt,
                  completed_at=datetime.now(timezone.utc).isoformat(), devices=results)
    (args.output/'results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    failures = sum(r['status'] != 'passed' for r in results)
    suite = ET.Element('testsuite', name='Hardware validation', tests=str(len(results)), failures=str(failures))
    for result in results:
        case = ET.SubElement(suite, 'testcase', name=result['variant'], classname=args.repository)
        if result['status'] != 'passed':
            ET.SubElement(case, 'failure', message=result.get('error', 'incomplete hardware evidence'))
    ET.ElementTree(suite).write(args.output/'junit.xml', encoding='utf-8', xml_declaration=True)
    return 1 if failures or not results else 0


if __name__ == '__main__':
    raise SystemExit(main())
