#!/usr/bin/env python3
"""Capture a development display over USB (Windows, macOS, Linux).

Use --name NAME for one capture, or omit it for an interactive console accepting
`capture NAME` and `quit`. Only checksum-verified frames are written. No raw
device logs are saved. Install pyserial; close other monitors using the port.
"""
import argparse
import hashlib
import json
import queue
import re
import struct
import sys
import threading
import time
import zlib
from pathlib import Path

PRODUCT_SIZES = {"LKBX": (320, 240), "DTT": (128, 64),
                 "OSSM": (128, 64), "RADR": (320, 240)}
RECORD = re.compile(r"(LKBX|DTT|OSSM|RADR)_SCREEN_(BEGIN|ROW|END|ERROR) (.*)$")


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def png_bytes(raw, width, height):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    scanlines = bytearray()
    for y in range(height):
        scanlines.append(0)
        for x in range(width):
            color = struct.unpack_from('<H', raw, (y * width + x) * 2)[0]
            r, g, b = color >> 11, (color >> 5) & 63, color & 31
            scanlines.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))


def decode_row(encoded, width):
    if not re.fullmatch(r'[0-9a-f]+', encoded) or len(encoded) % 7:
        raise ValueError('Corrupt RLE row')
    pixels = bytearray()
    for i in range(0, len(encoded), 7):
        count, color = int(encoded[i:i+3], 16), int(encoded[i+3:i+7], 16)
        if count < 1 or len(pixels) + count * 2 > width * 2:
            raise ValueError('Invalid RLE run')
        pixels.extend(struct.pack('<H', color) * count)
    if len(pixels) != width * 2:
        raise ValueError('Incomplete framebuffer row')
    return bytes(pixels)


class Decoder:
    def __init__(self, product=None, recover=False):
        self.product = product
        self.frame = None
        self.recover = recover
        self.snapshots = {}

    def feed(self, line):
        match = RECORD.search(line.strip())
        if not match:
            return None
        product, kind, payload = match.groups()
        if self.product and product != self.product:
            return None
        fields = payload.split()
        if kind == 'ERROR':
            self.frame = None
            raise ValueError('Device capture error: ' + payload)
        if kind == 'BEGIN':
            self.frame = None
            if not re.fullmatch(r'\d+ \d+ \d+ [0-9a-f]{8}', payload):
                raise ValueError('Malformed frame header')
            identity, width, height = map(int, fields[:3])
            if (width, height) != PRODUCT_SIZES[product]:
                raise ValueError('Unexpected framebuffer dimensions')
            checksum = int(fields[3], 16)
            key = (product, width, height, checksum)
            if key not in self.snapshots and len(self.snapshots) >= 3:
                self.snapshots.pop(next(iter(self.snapshots)))
            rows = self.snapshots.setdefault(key, {}) if self.recover else {}
            self.frame = {'product': product, 'id': identity, 'width': width, 'height': height,
                          'fnv1a': checksum, 'rows': rows, 'seen': set()}
            return None
        frame = self.frame
        if not frame or frame['product'] != product:
            return None
        if not fields or not fields[0].isdigit() or int(fields[0]) != frame['id']:
            return None
        if kind == 'ROW':
            if len(fields) != 3 or not fields[1].isdigit():
                if self.recover:
                    return None # Logs may interrupt the row's framing too.
                raise ValueError('Malformed frame row')
            row = int(fields[1])
            if not 0 <= row < frame['height'] or row in frame['seen']:
                raise ValueError('Duplicate or out-of-range row')
            try:
                pixels = decode_row(fields[2], frame['width'])
            except ValueError:
                if self.recover:
                    return None # A log-interrupted row may be recovered later.
                raise
            frame['rows'][row] = pixels
            frame['seen'].add(row)
        elif kind == 'END':
            self.frame = None
            if len(fields) != 1 or len(frame['rows']) != frame['height']:
                raise ValueError('Incomplete frame')
            raw = b''.join(frame['rows'][y] for y in range(frame['height']))
            if fnv1a(raw) != frame['fnv1a']:
                frame['rows'].clear()
                raise ValueError('Framebuffer checksum mismatch')
            return {key: value for key, value in frame.items() if key not in ('rows', 'seen')}, raw
        return None


def save_frame(directory, name, result):
    metadata, raw = result
    stem = directory / name
    metadata = dict(metadata, format='rgb565-le', sha256=hashlib.sha256(raw).hexdigest())
    stem.with_suffix('.rgb565').write_bytes(raw)
    stem.with_suffix('.png').write_bytes(png_bytes(raw, metadata['width'], metadata['height']))
    stem.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    return stem.with_suffix('.png')


def stdin_reader(commands):
    # select.select(stdin) is not supported by Windows. A daemon reader also
    # works with piped commands and never blocks serial polling or timeouts.
    for line in sys.stdin:
        commands.put(line.strip())


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--product', choices=PRODUCT_SIZES)
    parser.add_argument('--name', help='Capture once and exit; otherwise use the interactive console')
    parser.add_argument('--startup-delay', type=float, default=2)
    parser.add_argument('--duration', type=int, default=300)
    args = parser.parse_args()
    if not 1 <= args.duration <= 1800 or not 0 <= args.startup_delay <= 120:
        parser.error('duration must be 1..1800 seconds; startup-delay must be 0..120')
    if args.name and not re.fullmatch(r'[a-zA-Z0-9_-]{1,60}', args.name):
        parser.error('Use a filename label with letters, digits, - or _')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
    connection.dtr = connection.rts = False
    connection.port = args.port
    connection.open()
    if sys.platform == 'win32':
        # Native USB can burst an entire TFT frame faster than the Windows
        # default receive queue is drained, especially during local builds.
        connection.set_buffer_size(rx_size=1024 * 1024, tx_size=4096)
    commands = queue.Queue()
    if args.name:
        commands.put('capture ' + args.name)
    else:
        threading.Thread(target=stdin_reader, args=(commands,), daemon=True).start()
        print('Wait for boot, then type: capture NAME; quit closes the console.', flush=True)
    pending = None
    decoder = Decoder(args.product, recover=True)
    buffered = bytearray()
    discard_line = False
    with connection:
        time.sleep(args.startup_delay)
        connection.reset_input_buffer()
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            if not commands.empty():
                command = commands.get_nowait()
                if command == 'quit':
                    return 0
                if command.startswith('capture ') and not pending:
                    label = command[8:]
                    if re.fullmatch(r'[a-zA-Z0-9_-]{1,60}', label):
                        pending = (label, time.monotonic() + 90, 1)
                        decoder = Decoder(args.product, recover=True)
                        connection.write(b'\nscreen\n')
                    else:
                        print('Invalid filename label.', flush=True)
                elif command:
                    print('Use capture NAME or quit. One capture at a time.', flush=True)
            data = connection.read(min(connection.in_waiting or 1, 4096))
            buffered.extend(data)
            # Split chunks in C instead of iterating over every byte in Python;
            # native USB can deliver long TFT rows faster than bytewise polling.
            while b'\n' in buffered:
                raw_line, _, remainder = buffered.partition(b'\n')
                buffered = bytearray(remainder)
                line = raw_line.decode('ascii', errors='replace') if not discard_line and len(raw_line) <= 4096 else ''
                discard_line = False
                if not pending:
                    continue
                try:
                    result = decoder.feed(line)
                    if result:
                        image = save_frame(args.output_dir, pending[0], result)
                        connection.write(b'\nscreen release\n')
                        print(f'Verified {result[0]["width"]}x{result[0]["height"]}: {image}', flush=True)
                        pending = None
                        if args.name:
                            return 0
                except ValueError as error:
                    if (str(error) in ('Incomplete frame', 'Framebuffer checksum mismatch')
                            and pending[2] < 3 and time.monotonic() < pending[1]):
                        pending = (pending[0], pending[1], pending[2] + 1)
                        print('Incomplete/corrupt transfer; retransmitting the retained snapshot.', flush=True)
                        connection.write(b'\nscreen retry\n')
                        continue
                    print(f'Capture rejected: {error}; no image saved.', flush=True)
                    pending = None
                    decoder = Decoder(args.product, recover=True)
                    if args.name:
                        return 2
            if len(buffered) > 4096:
                buffered.clear()
                discard_line = True
            if pending and time.monotonic() > pending[1]:
                print('Capture timed out; no image saved.', flush=True)
                pending = None
                decoder = Decoder(args.product, recover=True)
                if args.name:
                    return 2
    return 2 if args.name or pending else 0


if __name__ == '__main__':
    raise SystemExit(main())
