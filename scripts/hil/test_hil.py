import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, MagicMock
from write import write_verified

from bundle import validate, package, merge, partition_rows, validate_partitions
from catalog import PRODUCTS, REPOSITORIES, policy, regions, OSSM_4MB, physical_flash_allowed
from monitor import Monitor, EvidenceError, PREFIX
from run import load_fixture, rig_lock, safe_line, test_device, main

SHA = 'a'*40
MAC = 'AABBCCDDEEFF'


def image(spec):
    data = bytearray(256)
    data[0], data[23] = 0xe9, 1
    data[3] = (2 if spec['flash_bytes'] == 4*1024*1024 else 4) << 4
    struct.pack_into('<H', data, 12, 9 if spec['chip'] == 'esp32s3' else 0)
    return bytes(data)+hashlib.sha256(data).digest()


def make_bundle(root, repository):
    for variant in PRODUCTS[REPOSITORIES[repository]]['variants']:
        spec = policy(repository, variant)
        source = root/'build'/variant
        source.mkdir(parents=True)
        for filename in regions(spec):
            data = b'non-executable fixture'
            if filename in ('firmware.bin', 'bootloader.bin'):
                data = image(spec)
            elif filename == 'partitions.bin':
                data = b''.join(struct.pack('<HBBII16sI', 0x50aa, row[1], row[2], row[3], row[4], row[0].encode(), 0)
                                for row in spec['partitions'])+b'\xff'*32
            (source/filename).write_bytes(data)
        args = argparse.Namespace(repository=repository, variant=variant, build_sha=SHA,
                                  run_id='42', run_attempt='1', output=root/'bundle', build_dir=source)
        package(args)
    merge(args)
    return root/'bundle'


class BundleTests(unittest.TestCase):
    def test_ossm_legacy_layout_is_not_silently_migrated(self):
        data = b''.join(struct.pack('<HBBII16sI', 0x50aa, row[1], row[2], row[3], row[4], row[0].encode(), 0)
                        for row in OSSM_4MB)+b'\xff'*32
        validate_partitions(data, policy('researchanddesire/OSSM', '4mb'))
        with self.assertRaisesRegex(ValueError, 'partition layout differs'):
            validate_partitions(data, policy('researchanddesire/OSSM', '16mb'))

    def test_public_trainer_keeps_its_export_reserve(self):
        repo = 'researchanddesire/DT_Trainer-OSS'
        with tempfile.TemporaryDirectory() as directory:
            bundle = make_bundle(Path(directory), repo)
            manifest = json.loads((bundle/'manifest.json').read_text())
            body = image(policy(repo, 'v1'))[:-32].ljust(0x140000-8192-32, b'\0')
            data = body+hashlib.sha256(body).digest()
            (bundle/'v1/firmware.bin').write_bytes(data)
            entry = manifest['variants']['v1']
            entry['components']['firmware.bin'].update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
            entry['image_sha256'] = data[-32:].hex()
            (bundle/'manifest.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, '16 KiB OTA reserve'):
                validate(bundle, repo, SHA, '42', '1')

    def test_every_product_variant_packages_and_validates(self):
        for repo in REPOSITORIES:
            with self.subTest(repo=repo), tempfile.TemporaryDirectory() as directory:
                bundle = make_bundle(Path(directory), repo)
                result = validate(bundle, repo, SHA, '42', '1')
                self.assertEqual(set(result['variants']), set(PRODUCTS[REPOSITORIES[repo]]['variants']))

    def test_rejects_wrong_repository_sha_run_and_attempt(self):
        repo = 'researchanddesire/DT_Trainer'
        with tempfile.TemporaryDirectory() as directory:
            bundle = make_bundle(Path(directory), repo)
            for identity in [(repo, 'b'*40, '42', '1'), (repo, SHA, '43', '1'),
                             (repo, SHA, '42', '2'), ('researchanddesire/DT_Trainer-OSS', SHA, '42', '1')]:
                with self.subTest(identity=identity), self.assertRaises(ValueError):
                    validate(bundle, *identity)

    def test_corruption_wrong_flash_chip_offset_or_missing_variant_fails(self):
        repo = 'researchanddesire/DT_Trainer'
        for mutation in ('corrupt', 'flash', 'chip', 'offset', 'missing', 'overflow', 'partition'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                bundle = make_bundle(Path(directory), repo)
                manifest = json.loads((bundle/'manifest.json').read_text())
                entry = manifest['variants']['v1']
                if mutation == 'missing':
                    del manifest['variants']['v2']
                elif mutation == 'offset':
                    entry['components']['firmware.bin']['offset'] = 0x9000
                else:
                    filename = 'partitions.bin' if mutation == 'partition' else 'firmware.bin'
                    data = bytearray((bundle/'v1'/filename).read_bytes())
                    if mutation == 'flash': data[3] = 0x40
                    elif mutation == 'chip': data[12] = 9
                    elif mutation == 'partition': data[8] ^= 1
                    elif mutation == 'overflow': data = bytearray(0x140001)
                    else: data[100] ^= 1
                    (bundle/'v1'/filename).write_bytes(data)
                    if mutation != 'corrupt':
                        entry['components'][filename].update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
                (bundle/'manifest.json').write_text(json.dumps(manifest))
                with self.assertRaises(ValueError): validate(bundle, repo, SHA, '42', '1')


class MonitorTests(unittest.TestCase):
    def monitor(self):
        expected = dict(schema=1, product='dtt', variant='v1', device_id=MAC,
                        flash_bytes=4194304, build_sha=SHA, image_sha256='b'*64, track='staging')
        monitor = Monitor(expected, 0)
        monitor.feed(PREFIX+json.dumps(dict(expected, event='boot', boot_id=1, uptime_ms=10)), .01)
        return monitor

    def heartbeat(self, monitor, now, **changes):
        data = dict(event='heartbeat', boot_id=1, uptime_ms=int(now*1000), ready=True,
                    app_age_ms=50, wifi=True, bench_wifi=True, ip='192.168.1.10',
                    internet=True, network_age_ms=1000, disconnects=0)
        data.update(changes)
        monitor.feed(PREFIX+json.dumps(data), now)

    def test_requires_full_host_and_device_observation(self):
        monitor = self.monitor()
        for now in range(5, 185, 5): self.heartbeat(monitor, now)
        self.assertFalse(monitor.passed)
        self.heartbeat(monitor, 185)
        self.assertTrue(monitor.passed)

    def test_host_time_cannot_replace_device_uptime(self):
        monitor = self.monitor()
        for now in range(5, 190, 5): self.heartbeat(monitor, now, uptime_ms=now*100)
        self.assertFalse(monitor.passed)

    def test_faults_and_network_losses_cannot_restart_window(self):
        failures = [dict(boot_id=2), dict(uptime_ms=1), dict(ready=False), dict(app_age_ms=20001),
                    dict(wifi=False), dict(bench_wifi=False), dict(ip='0.0.0.0'),
                    dict(internet=False), dict(network_age_ms=60001), dict(disconnects=1)]
        for change in failures:
            with self.subTest(change=change):
                monitor = self.monitor(); self.heartbeat(monitor, 5)
                with self.assertRaises(EvidenceError): self.heartbeat(monitor, 10, **change)

    def test_missing_malformed_boot_or_fault_text_fails(self):
        for line in ('RAD_HEALTH {}', 'RAD_HEALTH [1]', 'RAD_HEALTH {', 'Guru Meditation', 'watchdog', 'rst:0x1'):
            with self.subTest(line=line), self.assertRaises(EvidenceError):
                self.monitor().feed(line, 10)
        monitor = self.monitor(); self.heartbeat(monitor, 5)
        with self.assertRaises(EvidenceError): monitor.tick(26)
        with self.assertRaises(EvidenceError): self.monitor().tick(181)

    def test_wrong_running_identity_and_second_boot_fail(self):
        monitor = self.monitor()
        record = dict(monitor.expected, event='boot', boot_id=2, uptime_ms=10)
        with self.assertRaises(EvidenceError): monitor.feed(PREFIX+json.dumps(record), 11)
        fresh = Monitor(monitor.expected, 0)
        record['device_id'] = '112233445566'
        with self.assertRaises(EvidenceError): fresh.feed(PREFIX+json.dumps(record), 1)


class HostTests(unittest.TestCase):
    def test_larger_trainer_requires_explicit_enrollment_exception(self):
        spec = policy('researchanddesire/DT_Trainer', 'v1')
        self.assertFalse(physical_flash_allowed(spec, 16777216))
        self.assertTrue(physical_flash_allowed(spec, 16777216, True))
        self.assertFalse(physical_flash_allowed(spec, 8388608, True))
        self.assertFalse(physical_flash_allowed(policy('researchanddesire/OSSM', '4mb'), 16777216, True))
    def test_swapped_device_cannot_reach_write_flash(self):
        spec = policy('researchanddesire/DT_Trainer', 'v1')
        connected = MagicMock()
        connected.run_stub.return_value = connected
        connected.CHIP_NAME = 'ESP32'
        connected.read_mac.return_value = bytes.fromhex('112233445566')
        with patch('write.esptool.detect_chip', return_value=connected), patch('write.esptool.main') as writer:
            with self.assertRaisesRegex(RuntimeError, 'factory MAC changed'):
                write_verified('COM99', spec, MAC, Path('bundle'))
            writer.assert_not_called()
            connected._port.close.assert_called_once()

    def test_sensitive_unstructured_logs_are_redacted(self):
        self.assertNotIn('secret-value', safe_line('password=secret-value'))
        self.assertNotIn('secret-value', safe_line('RAD_HEALTH {"password":"secret-value"}'))
        for key in ('event', 'build_sha', 'uptime_ms', 'ip', 'ready'):
            self.assertNotIn('secret-value', safe_line(PREFIX+json.dumps({key: 'secret-value'})))

    def test_missing_or_wrong_capacity_fixture_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory)/'fixture.json'
            fixture.write_text('{"products": {"dtt": {}}}')
            with self.assertRaises(ValueError): load_fixture(fixture, 'researchanddesire/DT_Trainer')

    def test_wrong_capacity_or_duplicate_identity_fails_enrollment(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory)/'fixture.json'
            devices = {}
            for n, variant in enumerate(('v1', 'v2')):
                spec = policy('researchanddesire/DT_Trainer', variant)
                devices[variant] = dict(product='dtt', chip='esp32', flash_bytes=spec['flash_bytes'],
                                        psram=None, enrolled=True, device_id=f'AABBCCDDEE{n:02X}',
                                        usb_vid=0x10c4, usb_pid=0xea60, usb_location=f'usb-{n}')
            fixture.write_text(json.dumps(dict(schema=1, products=dict(dtt=devices))))
            self.assertEqual(len(load_fixture(fixture, 'researchanddesire/DT_Trainer')), 2)
            devices['v1']['flash_bytes'] = 16777216
            fixture.write_text(json.dumps(dict(schema=1, products=dict(dtt=devices))))
            with self.assertRaises(ValueError): load_fixture(fixture, 'researchanddesire/DT_Trainer')
            devices['v1']['flash_bytes'] = 4194304
            devices['v2']['device_id'] = devices['v1']['device_id']
            fixture.write_text(json.dumps(dict(schema=1, products=dict(dtt=devices))))
            with self.assertRaises(ValueError): load_fixture(fixture, 'researchanddesire/DT_Trainer')

    def test_flash_interruption_or_serial_failure_is_not_retried(self):
        repo = 'researchanddesire/DT_Trainer'
        for stage in ('probe', 'flash', 'observe'):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                bundle = make_bundle(root, repo)
                manifest = validate(bundle, repo, SHA, '42', '1')
                with patch('run.find_port', return_value='COM99'), patch('run.probe') as probe, \
                     patch('run.flash') as flash, patch('run.observe') as observe:
                    {'probe': probe, 'flash': flash, 'observe': observe}[stage].side_effect = OSError('interrupted')
                    result = test_device('v1', dict(device_id=MAC, flash_bytes=4194304), manifest, bundle, root)
                    self.assertEqual(result['status'], 'failed')
                    self.assertEqual(result['error'], 'interrupted')
                    self.assertLessEqual(flash.call_count, 1)
                    if stage != 'observe': observe.assert_not_called()
                    self.assertTrue((root/'v1-result.json').exists())

    def test_missing_device_prevents_all_writes_and_produces_junit(self):
        repo = 'researchanddesire/DT_Trainer'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = make_bundle(root, repo)
            args = ['run.py', '--repository', repo, '--build-sha', SHA, '--run-id', '42', '--run-attempt', '1',
                    '--bundle', str(bundle), '--fixture', str(root/'fixture.json'),
                    '--output', str(root/'output'), '--lock-directory', str(root/'locks')]
            with patch('sys.argv', args), patch('run.load_fixture', return_value={'v1': {}, 'v2': {}}), \
                 patch('run.find_port', side_effect=['COM99', RuntimeError('missing device')]), patch('run.flash') as flash:
                self.assertEqual(main(), 1)
                flash.assert_not_called()
                self.assertIn('failures="1"', (root/'output/junit.xml').read_text())

    def test_os_lock_blocks_another_repository_process(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'shared.lock'
            with rig_lock(path):
                code = 'from pathlib import Path; from run import rig_lock\nwith rig_lock(Path(__import__("sys").argv[1])): pass'
                result = subprocess.run([sys.executable, '-c', code, str(path)], cwd=Path(__file__).parent,
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('another process owns', result.stderr)
            with rig_lock(path): pass


if __name__ == '__main__':
    unittest.main()
