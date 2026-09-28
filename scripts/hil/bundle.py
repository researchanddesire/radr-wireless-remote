"""Versioned data-only firmware bundles for the installed bench harness."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct

from catalog import PRODUCTS, REPOSITORIES, policy, regions


def digest(data):
    return hashlib.sha256(data).hexdigest()


def image_digest(data):
    if len(data) < 56 or data[0] != 0xe9 or data[23] != 1:
        raise ValueError('expected an ESP image with appended SHA-256')
    if hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise ValueError('application digest is invalid')
    return data[-32:].hex()


def partition_rows(data):
    rows = []
    for offset in range(0, len(data)-31, 32):
        magic, kind, subtype, address, size, name, flags = struct.unpack_from('<HBBII16sI', data, offset)
        if magic != 0x50aa:
            break
        if flags != 0:
            raise ValueError('unsupported partition flags')
        rows.append((name.rstrip(b'\0').decode('ascii'), kind, subtype, address, size))
    return rows


def validate_partitions(data, spec):
    if partition_rows(data) != spec['partitions']:
        raise ValueError('partition layout differs from the reviewed product layout')
    spans = sorted((r[3], r[3]+r[4]) for r in spec['partitions'])
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])) or spans[-1][1] > spec['flash_bytes']:
        raise ValueError('partition overlap or physical flash overflow')


def read_component(directory, variant, filename):
    root = directory.resolve()
    path = directory / variant / filename
    if path.is_symlink() or path.parent.is_symlink() or not path.resolve().is_relative_to(root):
        raise ValueError('artifact path escapes the bundle')
    return path.read_bytes()


def validate(directory, repository, build_sha, run_id, run_attempt, variants=None, manifest_name='manifest.json'):
    if repository not in REPOSITORIES or not re.fullmatch('[0-9a-f]{40}', build_sha):
        raise ValueError('invalid repository or build SHA')
    if not all(re.fullmatch('[1-9][0-9]*', str(v)) for v in (run_id, run_attempt)):
        raise ValueError('invalid run identity')
    manifest = json.loads((directory / manifest_name).read_text(encoding='utf-8'))
    expected = dict(schema=2, repository=repository, build_sha=build_sha,
                    run_id=str(run_id), run_attempt=str(run_attempt), track='staging',
                    product=REPOSITORIES[repository])
    if any(manifest.get(k) != v for k, v in expected.items()):
        raise ValueError('bundle identity differs from this workflow run')
    variants = set(variants or PRODUCTS[expected['product']]['variants'])
    if set(manifest.get('variants', {})) != variants:
        raise ValueError('missing or unexpected hardware variant')
    for variant in variants:
        spec = policy(repository, variant)
        entry = manifest['variants'][variant]
        if any(entry.get(k) != spec[k] for k in ('chip', 'flash_bytes', 'environment', 'psram')):
            raise ValueError('variant configuration mismatch')
        components = regions(spec)
        if set(entry.get('components', {})) != set(components):
            raise ValueError('missing or unexpected bundle component')
        for filename, (address, limit) in components.items():
            data = read_component(directory, variant, filename)
            if not 0 < len(data) <= limit:
                raise ValueError('component size exceeds approved region: '+filename)
            if entry['components'][filename] != dict(offset=address, size=len(data), sha256=digest(data)):
                raise ValueError('component metadata/checksum mismatch: '+filename)
            if filename in ('firmware.bin', 'bootloader.bin'):
                expected_chip = 9 if spec['chip'] == 'esp32s3' else 0
                if (len(data) < 24 or data[0] != 0xe9 or data[3] >> 4 != {4: 2, 16: 4}[spec['flash_bytes']//(1024*1024)]
                        or struct.unpack_from('<H', data, 12)[0] != expected_chip):
                    raise ValueError('image chip/physical flash header mismatch')
        app = read_component(directory, variant, 'firmware.bin')
        if entry.get('image_sha256') != image_digest(app):
            raise ValueError('application runtime identity mismatch')
        validate_partitions(read_component(directory, variant, 'partitions.bin'), spec)
        # The public Trainer export contract reserves 16 KiB. The private
        # Trainer enforces its deployed slot boundary without that export rule.
        if repository == 'researchanddesire/DT_Trainer-OSS' and len(app)+16384 > regions(spec)['firmware.bin'][1]:
            raise ValueError('public Trainer image lacks the required 16 KiB OTA reserve')
    return manifest


def package(args):
    spec = policy(args.repository, args.variant)
    entry = {key: spec[key] for key in ('chip', 'flash_bytes', 'environment', 'psram')}
    entry['components'] = {}
    dest = args.output / args.variant
    dest.mkdir(parents=True, exist_ok=True)
    for filename, (address, _) in regions(spec).items():
        source = args.build_dir / filename
        if filename == 'boot_app0.bin' and not source.exists():
            core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home()/'.platformio'))
            source = core / 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
        data = source.read_bytes()
        (dest / filename).write_bytes(data)
        entry['components'][filename] = dict(offset=address, size=len(data), sha256=digest(data))
    entry['image_sha256'] = image_digest((dest/'firmware.bin').read_bytes())
    manifest = dict(schema=2, repository=args.repository, build_sha=args.build_sha,
                    run_id=str(args.run_id), run_attempt=str(args.run_attempt),
                    product=spec['product'], track='staging', variants={args.variant: entry})
    name = 'manifest-'+args.variant+'.json'
    (args.output/name).write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    validate(args.output, args.repository, args.build_sha, args.run_id, args.run_attempt, [args.variant], name)


def merge(args):
    product = REPOSITORIES[args.repository]
    manifest = None
    combined = {}
    for variant in PRODUCTS[product]['variants']:
        part = validate(args.output, args.repository, args.build_sha, args.run_id,
                        args.run_attempt, [variant], 'manifest-'+variant+'.json')
        combined.update(part['variants'])
        manifest = part
    manifest['variants'] = combined
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    validate(args.output, args.repository, args.build_sha, args.run_id, args.run_attempt)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True, choices=REPOSITORIES)
    parser.add_argument('--build-sha', required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--run-attempt', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--variant')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--merge', action='store_true')
    args = parser.parse_args()
    if not args.merge and (not args.variant or not args.build_dir):
        parser.error('packaging requires --variant and --build-dir')
    merge(args) if args.merge else package(args)
