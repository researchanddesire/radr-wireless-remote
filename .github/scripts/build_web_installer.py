#!/usr/bin/env python3
"""Build and validate a merged ESP Web Tools factory image."""

from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Profile:
    chip: str
    flash_size: str
    capacity: int
    bootloader_offset: int
    flash_mode: str
    flash_freq: str
    chip_id: int
    flash_mode_id: int
    flash_freq_id: int
    flash_size_id: int


PROFILES = {
    "esp32-4mb": Profile(
        "esp32", "4MB", 4 * 1024 * 1024, 0x1000, "dio", "40m", 0, 2, 0, 2
    ),
    "esp32-16mb": Profile(
        "esp32", "16MB", 16 * 1024 * 1024, 0x1000, "dio", "40m", 0, 2, 0, 4
    ),
    "esp32s3-16mb": Profile(
        # The pinned PlatformIO platform emits a hash-protected DIO bootloader
        # for this QIO-capable module. Preserve that boot header during merging;
        # the application configures the runtime memory interface separately.
        "esp32s3", "16MB", 16 * 1024 * 1024, 0x0, "dio", "80m", 9, 2, 15, 4
    ),
}


def require_image(path: Path, label: str) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f"{label} image is missing or empty: {path}")
    return path


def find_boot_app0() -> Path:
    platformio_home = Path(
        os.environ.get("PLATFORMIO_HOME_DIR", Path.home() / ".platformio")
    )
    active = (
        platformio_home
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "partitions"
        / "boot_app0.bin"
    )
    if active.is_file():
        return require_image(active, "boot_app0")
    candidates = sorted(
        platformio_home.glob(
            "packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin"
        )
    )
    if len(candidates) != 1:
        raise RuntimeError(
            f"expected one PlatformIO boot_app0.bin, found {len(candidates)}"
        )
    return require_image(candidates[0], "boot_app0")


def find_esptool() -> Path:
    platformio_home = Path(
        os.environ.get("PLATFORMIO_HOME_DIR", Path.home() / ".platformio")
    )
    active = platformio_home / "packages" / "tool-esptoolpy" / "esptool.py"
    if active.is_file():
        return require_image(active, "esptool")
    candidates = sorted(
        platformio_home.glob("packages/tool-esptoolpy@*/esptool.py")
    )
    if len(candidates) != 1:
        raise RuntimeError(f"expected one PlatformIO esptool.py, found {len(candidates)}")
    return require_image(candidates[0], "esptool")


def parse_partition_number(value: str) -> int:
    normalized = value.strip().upper()
    multiplier = 1
    if normalized.endswith("K"):
        normalized = normalized[:-1]
        multiplier = 1024
    elif normalized.endswith("M"):
        normalized = normalized[:-1]
        multiplier = 1024 * 1024
    return int(normalized, 0) * multiplier


def partition_layout(path: Path, partition_name: str) -> tuple[int, int]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = csv.reader(line for line in handle if not line.lstrip().startswith("#"))
        for row in rows:
            if row and row[0].strip() == partition_name:
                if len(row) < 5:
                    raise RuntimeError(
                        f"partition {partition_name!r} has no offset and size in {path}"
                    )
                return parse_partition_number(row[3]), parse_partition_number(row[4])
    raise RuntimeError(f"partition {partition_name!r} was not found in {path}")


def build_components(
    profile: Profile,
    build_dir: Path,
    boot_app0: Path,
    filesystem: Path | None = None,
    filesystem_offset: int | None = None,
    filesystem_size: int | None = None,
) -> list[tuple[int, Path]]:
    components = [
        (profile.bootloader_offset, require_image(build_dir / "bootloader.bin", "bootloader")),
        (0x8000, require_image(build_dir / "partitions.bin", "partition table")),
        (0xE000, require_image(boot_app0, "boot_app0")),
        (0x10000, require_image(build_dir / "firmware.bin", "application")),
    ]
    if filesystem is not None:
        if filesystem_offset is None:
            raise RuntimeError("a filesystem offset is required with a filesystem image")
        filesystem = require_image(filesystem, "filesystem")
        if filesystem_size is None:
            raise RuntimeError("a filesystem partition size is required")
        if filesystem.stat().st_size > filesystem_size:
            raise RuntimeError(
                f"filesystem image is {filesystem.stat().st_size} bytes, beyond its "
                f"{filesystem_size}-byte partition"
            )
        components.append((filesystem_offset, filesystem))
    components = sorted(components)
    previous_end = 0
    for offset, component in components:
        if offset < previous_end:
            raise RuntimeError(f"merged component overlaps at {hex(offset)}")
        component_end = offset + component.stat().st_size
        if component_end > profile.capacity:
            raise RuntimeError(
                f"{component.name} ends at {hex(component_end)}, beyond "
                f"{profile.capacity}-byte flash"
            )
        previous_end = component_end
    return components


def merge_command(
    profile: Profile,
    output: Path,
    components: list[tuple[int, Path]],
    esptool: Path | None = None,
) -> list[str]:
    command = [
        sys.executable,
        str(esptool or find_esptool()),
        "--chip",
        profile.chip,
        "merge_bin",
        "--output",
        str(output),
        "--flash_mode",
        "keep",
        "--flash_freq",
        "keep",
        "--flash_size",
        profile.flash_size,
    ]
    for offset, path in components:
        command.extend((hex(offset), str(path)))
    return command


def validate_merged_image(
    output: Path, profile: Profile, components: list[tuple[int, Path]]
) -> None:
    content = require_image(output, "merged web installer").read_bytes()
    if len(content) > profile.capacity:
        raise RuntimeError(
            f"merged image is {len(content)} bytes, beyond {profile.capacity}-byte flash"
        )

    for offset in (profile.bootloader_offset, 0x10000):
        if offset >= len(content) or content[offset] != 0xE9:
            raise RuntimeError(f"ESP image magic is missing at {hex(offset)}")
        chip_id = int.from_bytes(content[offset + 12 : offset + 14], "little")
        if chip_id != profile.chip_id:
            raise RuntimeError(
                f"ESP chip ID {chip_id} at {hex(offset)} does not match "
                f"{profile.chip} ({profile.chip_id})"
            )

    header = content[profile.bootloader_offset : profile.bootloader_offset + 4]
    if header[2] != profile.flash_mode_id:
        raise RuntimeError(
            f"flash mode ID {header[2]} does not match {profile.flash_mode}"
        )
    flash_size_id = header[3] >> 4
    flash_freq_id = header[3] & 0x0F
    if flash_size_id != profile.flash_size_id:
        raise RuntimeError(
            f"flash size ID {flash_size_id} does not match {profile.flash_size}"
        )
    if flash_freq_id != profile.flash_freq_id:
        raise RuntimeError(
            f"flash frequency ID {flash_freq_id} does not match {profile.flash_freq}"
        )

    for offset, source_path in components:
        source = source_path.read_bytes()
        merged = content[offset : offset + len(source)]
        if merged != source:
            raise RuntimeError(f"merged component mismatch at {hex(offset)}")


def build(args: argparse.Namespace) -> None:
    profile = PROFILES[args.profile]
    filesystem_offset = None
    filesystem_size = None
    if args.filesystem:
        if not args.partition_table:
            raise RuntimeError("--partition-table is required with --filesystem")
        filesystem_offset, filesystem_size = partition_layout(
            args.partition_table, args.filesystem_partition
        )
    boot_app0 = args.boot_app0 or find_boot_app0()
    components = build_components(
        profile,
        args.build_dir,
        boot_app0,
        args.filesystem,
        filesystem_offset,
        filesystem_size,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(merge_command(profile, args.output, components), check=True)
    validate_merged_image(args.output, profile, components)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True, choices=tuple(PROFILES))
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--boot-app0", type=Path)
    parser.add_argument("--filesystem", type=Path)
    parser.add_argument("--partition-table", type=Path)
    parser.add_argument("--filesystem-partition", default="spiffs")
    args = parser.parse_args()
    try:
        build(args)
        print(f"Built validated web installer: {args.output}")
        return 0
    except Exception as error:
        print(f"Web installer build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
