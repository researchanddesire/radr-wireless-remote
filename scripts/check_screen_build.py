#!/usr/bin/env python3
"""Check the compiled image contains the development console only when enabled."""
import argparse
from pathlib import Path


def check(build_dir, enabled):
    image = (build_dir / 'firmware.bin').read_bytes()
    if not image or image[0] != 0xe9:
        raise ValueError('Expected an ESP firmware image')
    for marker in (b'%s_SCREEN_BEGIN ', b'%s_SCREEN_ROW ', b'%s_SCREEN_END '):
        if (marker in image) != enabled:
            raise ValueError('Development screen console inclusion differs from this profile')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--expect', choices=('enabled', 'disabled'), required=True)
    args = parser.parse_args()
    check(args.build_dir, args.expect == 'enabled')
    print('Development screen console: ' + args.expect)
