import io
import queue
import struct
import unittest
import tempfile
import types
import json
from pathlib import Path
from unittest.mock import patch

from capture_screen import Decoder, decode_row, fnv1a, png_bytes, stdin_reader


class CaptureTests(unittest.TestCase):
    def records(self, product='DTT', width=128, height=64, color=0xffff, identity=42):
        raw = struct.pack('<H', color) * width * height
        return ([f'{product}_SCREEN_BEGIN {identity} {width} {height} {fnv1a(raw):08x}'] +
                [f'{product}_SCREEN_ROW {identity} {y} {width:03x}{color:04x}' for y in range(height)] +
                [f'{product}_SCREEN_END {identity}']), raw

    def test_all_products_and_log_noise(self):
        for product, width, height in [('LKBX', 320, 240), ('RADR', 320, 240),
                                       ('DTT', 128, 64), ('OSSM', 128, 64)]:
            records, raw = self.records(product, width, height)
            decoder = Decoder(product)
            result = None
            for line in records:
                self.assertIsNone(decoder.feed('unrelated log'))
                result = decoder.feed(line)
            self.assertEqual(result[1], raw)

    def test_missing_row_and_stale_id_rejected(self):
        records, _ = self.records()
        decoder = Decoder()
        decoder.feed(records[0])
        decoder.feed(records[1].replace('ROW 42', 'ROW 41'))
        for line in records[2:-1]:
            decoder.feed(line)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            decoder.feed(records[-1])

    def test_corruption_rejected(self):
        records, _ = self.records()
        records[1] = records[1].replace('080ffff', '0800000')
        decoder = Decoder()
        for line in records[:-1]:
            decoder.feed(line)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            decoder.feed(records[-1])

    def test_snapshots_never_mix(self):
        first, _ = self.records(identity=1)
        second, _ = self.records(identity=2)
        decoder = Decoder()
        for line in first[:33] + second[:1] + second[33:-1]:
            decoder.feed(line)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            decoder.feed(second[-1])

    def test_dimensions_duplicate_and_bad_runs(self):
        with self.assertRaisesRegex(ValueError, 'dimensions'):
            Decoder().feed('DTT_SCREEN_BEGIN 1 65535 65535 12345678')
        records, _ = self.records()
        decoder = Decoder()
        decoder.feed(records[0])
        decoder.feed(records[1])
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            decoder.feed(records[1])
        for value in ['000ffff', '081ffff', '080fff', '001ffff', '080zzzz']:
            with self.assertRaises(ValueError):
                decode_row(value, 128)

    def test_foreign_product_ignored_and_device_error_reported(self):
        decoder = Decoder('DTT')
        self.assertIsNone(decoder.feed('RADR_SCREEN_ERROR no_memory'))
        with self.assertRaisesRegex(ValueError, 'no_memory'):
            decoder.feed('DTT_SCREEN_ERROR no_memory')

    def test_color_endianness_and_png(self):
        raw = decode_row('001f80000107e0001001f', 3)
        self.assertEqual(raw, b'\x00\xf8\xe0\x07\x1f\x00')
        self.assertTrue(png_bytes(raw, 3, 1).startswith(b'\x89PNG\r\n\x1a\n'))

    def test_windows_and_pipe_compatible_input(self):
        commands = queue.Queue()
        with patch('sys.stdin', io.StringIO('capture screen\nquit\n')):
            stdin_reader(commands)
        self.assertEqual(commands.get_nowait(), 'capture screen')
        self.assertEqual(commands.get_nowait(), 'quit')

    def test_same_snapshot_recovers_missing_rows(self):
        first, raw = self.records(identity=1)
        second, _ = self.records(identity=2)
        decoder = Decoder(recover=True)
        for line in first[:33]:
            decoder.feed(line)
        decoder.feed('DTT_SCREEN_ROW 1 33 080ffff interrupted log')
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            decoder.feed(first[-1])
        result = None
        for line in second[:1] + second[33:]:
            result = decoder.feed(line)
        self.assertEqual(result[1], raw)

    def test_recovery_cannot_mix_changed_images(self):
        first, _ = self.records(identity=1)
        second, _ = self.records(identity=2, color=0)
        decoder = Decoder(recover=True)
        for line in first[:33]:
            decoder.feed(line)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            decoder.feed(first[-1])
        for line in second[:1] + second[33:-1]:
            decoder.feed(line)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            decoder.feed(second[-1])

    def test_retransmission_retains_snapshot_identity(self):
        records, raw = self.records(identity=42)
        decoder = Decoder(recover=True)
        for line in records[:33]:
            decoder.feed(line)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            decoder.feed(records[-1])
        for line in records[:1] + records[33:]:
            result = decoder.feed(line)
        self.assertEqual(result[0]['id'], 42)
        self.assertEqual(result[1], raw)


    def test_windows_cli_handles_burst_with_maximum_rle_rows(self):
        import capture_screen
        raw = b'\xff\xff\x00\x00' * (160 * 240)
        records = [f'LKBX_SCREEN_BEGIN 42 320 240 {capture_screen.fnv1a(raw):08x}']
        records += [f'LKBX_SCREEN_ROW 42 {row} ' + '001ffff0010000' * 160
                    for row in range(240)]
        records += ['LKBX_SCREEN_END 42']
        burst = ('x' * 6000 + '\n' + '\n'.join(records) + '\n').encode('ascii')

        class Port:
            def __init__(self):
                self.data = b''
                self.commands = []
                self.buffers = None
            def open(self): pass
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def reset_input_buffer(self): self.data = b''
            def set_buffer_size(self, **kwargs): self.buffers = kwargs
            @property
            def in_waiting(self): return len(self.data)
            def read(self, size):
                result, self.data = self.data[:size], self.data[size:]
                return result
            def write(self, data):
                self.commands.append(data)
                if data == b'\nscreen\n': self.data += burst
                return len(data)

        port = Port()
        with tempfile.TemporaryDirectory() as directory:
            argv = ['capture_screen.py', '--port', 'COM_TEST', '--product', 'LKBX',
                    '--output-dir', directory, '--name', 'burst', '--startup-delay', '0']
            with patch.dict('sys.modules', {'serial': types.SimpleNamespace(Serial=lambda **kwargs: port)}), \
                    patch('sys.argv', argv), patch('sys.platform', 'win32'):
                self.assertEqual(capture_screen.main(), 0)
            self.assertEqual((Path(directory) / 'burst.rgb565').read_bytes(), raw)
            self.assertEqual(json.loads((Path(directory) / 'burst.json').read_text())['id'], 42)
        self.assertEqual(port.commands, [b'\nscreen\n', b'\nscreen release\n'])
        self.assertEqual(port.buffers['rx_size'], 1024 * 1024)


if __name__ == '__main__':
    unittest.main()
