#!/usr/bin/env python3
"""Build and validate installable RADR Buttplug firmware artifacts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
TARGET_ELF = Path("target/xtensa-esp32s3-espidf/release/radr-buttplug-poc")
PARTITION_TABLE = Path("partitions.csv")
APP_PARTITION = "app0"
REQUIRED_PARTITIONS = {"nvs", "otadata", "app0", "app1"}
ERASED_STATE_PARTITIONS = ("nvs", "otadata")
APP_OFFSET = 0x10000
PARTITION_TABLE_OFFSET = 0x8000
FLASH_CAPACITY = 16 * 1024 * 1024
ESP32S3_CHIP_ID = 9
FLASH_MODE_DIO_ID = 2
FLASH_SIZE_16MB_ID = 4
FLASH_FREQUENCY_80MHZ_ID = 15
ESP_IMAGE_MAGIC = 0xE9
PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
ESPFLASH_VERSION = "4.5.0"

FACTORY_IMAGE_NAME = "radr-buttplug-factory.bin"
APP_IMAGE_NAME = "radr-buttplug-app.bin"
MANIFEST_NAME = "manifest.json"
BUNDLE_METADATA_NAME = "bundle.json"
INSTALL_PAGE_NAME = "install.html"
CHECKSUMS_NAME = "SHA256SUMS"

BUTTPLUG_GIT_URL = "https://github.com/buttplugio/buttplug"
DIRECT_BUTTPLUG_DEPENDENCIES = {
    "buttplug_client",
    "buttplug_client_in_process",
    "buttplug_core",
    "buttplug_server",
    "buttplug_server_device_config",
}

PARTITION_TYPES = {
    "app": 0x00,
    "data": 0x01,
}
APP_SUBTYPES = {
    "factory": 0x00,
    "test": 0x20,
}
DATA_SUBTYPES = {
    "ota": 0x00,
    "phy": 0x01,
    "nvs": 0x02,
    "coredump": 0x03,
    "nvs_keys": 0x04,
    "efuse": 0x05,
    "undefined": 0x06,
    "esphttpd": 0x80,
    "fat": 0x81,
    "spiffs": 0x82,
    "littlefs": 0x83,
}


@dataclass(frozen=True)
class Partition:
    name: str
    type_id: int
    subtype_id: int
    offset: int
    size: int
    flags: int = 0


def parse_number(value: str) -> int:
    normalized = value.strip().upper()
    multiplier = 1
    if normalized.endswith("K"):
        normalized = normalized[:-1]
        multiplier = 1024
    elif normalized.endswith("M"):
        normalized = normalized[:-1]
        multiplier = 1024 * 1024
    if not normalized:
        raise ValueError("empty numeric value")
    return int(normalized, 0) * multiplier


def parse_partition_type(value: str) -> int:
    normalized = value.strip().lower()
    if normalized in PARTITION_TYPES:
        return PARTITION_TYPES[normalized]
    return parse_number(normalized)


def parse_partition_subtype(type_id: int, value: str) -> int:
    normalized = value.strip().lower()
    if type_id == PARTITION_TYPES["app"]:
        if normalized.startswith("ota_"):
            return 0x10 + parse_number(normalized.removeprefix("ota_"))
        if normalized in APP_SUBTYPES:
            return APP_SUBTYPES[normalized]
    elif type_id == PARTITION_TYPES["data"] and normalized in DATA_SUBTYPES:
        return DATA_SUBTYPES[normalized]
    return parse_number(normalized)


def parse_partition_flags(value: str) -> int:
    if not value.strip():
        return 0
    flags = 0
    for flag in re.split(r"[:|]", value.strip().lower()):
        if flag == "encrypted":
            flags |= 1
        elif flag:
            flags |= parse_number(flag)
    return flags


def read_partition_table(path: Path) -> list[Partition]:
    partitions: list[Partition] = []
    with path.open(newline="", encoding="utf-8") as handle:
        rows = csv.reader(line for line in handle if not line.lstrip().startswith("#"))
        for row in rows:
            if not row or not any(field.strip() for field in row):
                continue
            if len(row) < 5:
                raise RuntimeError(f"partition row has fewer than five fields: {row}")
            name = row[0].strip()
            if not name or len(name.encode("utf-8")) > 16:
                raise RuntimeError(f"invalid partition name: {name!r}")
            type_id = parse_partition_type(row[1])
            subtype_id = parse_partition_subtype(type_id, row[2])
            if not row[3].strip():
                raise RuntimeError(f"partition {name!r} must have an explicit offset")
            partitions.append(
                Partition(
                    name=name,
                    type_id=type_id,
                    subtype_id=subtype_id,
                    offset=parse_number(row[3]),
                    size=parse_number(row[4]),
                    flags=parse_partition_flags(row[5] if len(row) > 5 else ""),
                )
            )

    if not partitions:
        raise RuntimeError(f"partition table is empty: {path}")
    names = [partition.name for partition in partitions]
    if len(names) != len(set(names)):
        raise RuntimeError(f"partition names are not unique: {path}")

    previous_end = 0
    for partition in sorted(partitions, key=lambda item: item.offset):
        if partition.offset < previous_end:
            raise RuntimeError(f"partition {partition.name!r} overlaps its predecessor")
        previous_end = partition.offset + partition.size
        if previous_end > FLASH_CAPACITY:
            raise RuntimeError(f"partition {partition.name!r} exceeds 16 MB flash")
    return partitions


def parse_binary_partition_table(image: bytes) -> list[Partition]:
    entries: list[Partition] = []
    for offset in range(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + 0x1000, 32):
        entry = image[offset : offset + 32]
        if len(entry) != 32:
            raise RuntimeError("merged image ends inside the partition table")
        magic = struct.unpack_from("<H", entry)[0]
        if magic in (0xFFFF, PARTITION_MD5_MAGIC):
            break
        if magic != PARTITION_MAGIC:
            raise RuntimeError(f"invalid partition-table magic at {offset:#x}")
        _, type_id, subtype_id, part_offset, size, raw_name, flags = struct.unpack(
            "<HBBII16sI", entry
        )
        name = raw_name.split(b"\0", 1)[0].decode("utf-8")
        entries.append(
            Partition(name, type_id, subtype_id, part_offset, size, flags)
        )
    if not entries:
        raise RuntimeError("merged image has no partition entries")
    return entries


def validate_image_header(image: bytes, offset: int, label: str) -> None:
    if len(image) < offset + 16:
        raise RuntimeError(f"{label} image header is truncated")
    if image[offset] != ESP_IMAGE_MAGIC:
        raise RuntimeError(f"{label} image magic is missing at {offset:#x}")
    chip_id = int.from_bytes(image[offset + 12 : offset + 14], "little")
    if chip_id != ESP32S3_CHIP_ID:
        raise RuntimeError(
            f"{label} chip ID {chip_id} does not match ESP32-S3 ({ESP32S3_CHIP_ID})"
        )
    if image[offset + 2] != FLASH_MODE_DIO_ID:
        raise RuntimeError(f"{label} image is not configured for DIO flash mode")
    size_and_frequency = image[offset + 3]
    if size_and_frequency >> 4 != FLASH_SIZE_16MB_ID:
        raise RuntimeError(f"{label} image is not configured for 16 MB flash")
    if size_and_frequency & 0x0F != FLASH_FREQUENCY_80MHZ_ID:
        raise RuntimeError(f"{label} image is not configured for 80 MHz flash")


def validate_bundle_images(
    factory_image: Path, app_image: Path, partitions: list[Partition]
) -> None:
    factory = factory_image.read_bytes()
    app = app_image.read_bytes()
    if not app:
        raise RuntimeError("application image is empty")
    if len(factory) > FLASH_CAPACITY:
        raise RuntimeError("factory image exceeds 16 MB flash capacity")

    expected = {partition.name: partition for partition in partitions}
    missing_partitions = REQUIRED_PARTITIONS - expected.keys()
    if missing_partitions:
        raise RuntimeError(
            f"required partitions are missing: {sorted(missing_partitions)}"
        )
    actual = {
        partition.name: partition for partition in parse_binary_partition_table(factory)
    }
    if actual != expected:
        raise RuntimeError(
            "merged partition table does not match partitions.csv: "
            f"expected {expected}, got {actual}"
        )

    app_partition = expected.get(APP_PARTITION)
    if app_partition is None:
        raise RuntimeError(f"partition {APP_PARTITION!r} is missing")
    if app_partition.offset != APP_OFFSET:
        raise RuntimeError(
            f"partition {APP_PARTITION!r} must start at {APP_OFFSET:#x}"
        )
    if len(app) > app_partition.size:
        raise RuntimeError(
            f"application is {len(app)} bytes, beyond its {app_partition.size}-byte slot"
        )
    if len(factory) != app_partition.offset + len(app):
        raise RuntimeError("compact factory image does not end with the application image")
    if factory[app_partition.offset :] != app:
        raise RuntimeError("factory image application does not match the OTA image")

    for partition_name in ERASED_STATE_PARTITIONS:
        partition = expected[partition_name]
        contents = factory[partition.offset : partition.offset + partition.size]
        if len(contents) != partition.size or any(byte != 0xFF for byte in contents):
            raise RuntimeError(
                f"factory image partition {partition_name!r} is not erased"
            )

    validate_image_header(factory, 0, "bootloader")
    validate_image_header(factory, app_partition.offset, "application")
    validate_image_header(app, 0, "standalone application")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def project_metadata(project_root: Path) -> tuple[str, str]:
    cargo_source = (project_root / "Cargo.toml").read_text("utf-8")
    package_section = re.search(
        r"(?ms)^\[package\]\s*(.*?)(?=^\[)", cargo_source
    )
    version_match = (
        re.search(r'^version\s*=\s*"([^"]+)"', package_section.group(1), re.MULTILINE)
        if package_section
        else None
    )
    if version_match is None:
        raise RuntimeError("Cargo package version is missing")

    direct_revisions: dict[str, str] = {}
    for match in re.finditer(
        r'(?m)^(buttplug_[a-z_]+)\s*=\s*\{([^\n]+)\}\s*$', cargo_source
    ):
        name, attributes = match.groups()
        git_match = re.search(r'git\s*=\s*"([^"]+)"', attributes)
        revision_match = re.search(r'rev\s*=\s*"([0-9a-f]{40})"', attributes)
        if git_match and git_match.group(1) == BUTTPLUG_GIT_URL:
            if revision_match is None:
                raise RuntimeError(f"official dependency {name!r} has no exact revision")
            direct_revisions[name] = revision_match.group(1)

    actual_direct = set(direct_revisions)
    if actual_direct != DIRECT_BUTTPLUG_DEPENDENCIES:
        raise RuntimeError(
            "direct official Buttplug dependencies changed: "
            f"expected {sorted(DIRECT_BUTTPLUG_DEPENDENCIES)}, "
            f"got {sorted(actual_direct)}"
        )
    revisions = set(direct_revisions.values())
    if len(revisions) != 1:
        raise RuntimeError("official Buttplug dependencies must share one exact revision")
    revision = revisions.pop()

    main_source = (project_root / "src/main.rs").read_text("utf-8")
    match = re.search(
        r'const UPSTREAM_BUTTPLUG_REVISION: &str = "([0-9a-f]{40})";',
        main_source,
    )
    if match is None or match.group(1) != revision:
        raise RuntimeError("runtime Buttplug revision does not match Cargo.toml")
    return version_match.group(1), revision


def git_revision(project_root: Path) -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=project_root,
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def run(command: list[str], cwd: Path) -> None:
    print(f"+ {shlex.join(command)}", flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def require_espflash_version(espflash: str, project_root: Path) -> None:
    result = subprocess.run(
        [espflash, "--version"],
        cwd=project_root,
        check=True,
        capture_output=True,
        text=True,
    )
    if result.stdout.strip() != f"espflash {ESPFLASH_VERSION}":
        raise RuntimeError(
            f"expected espflash {ESPFLASH_VERSION}, got {result.stdout.strip()!r}"
        )


def espflash_image_command(
    espflash: str, elf: Path, output: Path, *, merge: bool
) -> list[str]:
    command = [
        espflash,
        "save-image",
        "--chip",
        "esp32s3",
        "--flash-size",
        "16mb",
        "--flash-mode",
        "dio",
        "--flash-freq",
        "80mhz",
        "--partition-table",
        str(PARTITION_TABLE),
        "--target-app-partition",
        APP_PARTITION,
    ]
    if merge:
        command.extend(("--merge", "--skip-padding"))
    command.extend((str(elf), str(output)))
    return command


def web_manifest(version: str) -> dict[str, object]:
    return {
        "name": "RADR Upstream Buttplug Rust Probe",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "improv": False,
                "parts": [{"path": FACTORY_IMAGE_NAME, "offset": 0}],
            }
        ],
    }


def install_page() -> str:
    return """<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Install RADR Upstream Buttplug Rust Probe</title>
    <script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
  </head>
  <body>
    <h1>RADR Upstream Buttplug Rust Probe</h1>
    <p>This experimental image replaces the complete firmware on a 16 MB ESP32-S3 RADR.</p>
    <esp-web-install-button manifest="manifest.json"></esp-web-install-button>
  </body>
</html>
"""


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def build_bundle(args: argparse.Namespace) -> list[Path]:
    project_root = args.project_root.resolve()
    output_dir = args.output_dir.resolve()
    if args.elf:
        elf = args.elf if args.elf.is_absolute() else project_root / args.elf
        elf = elf.resolve()
    else:
        elf = project_root / TARGET_ELF
    partitions_path = project_root / PARTITION_TABLE
    partitions = read_partition_table(partitions_path)
    version, upstream_revision = project_metadata(project_root)
    require_espflash_version(args.espflash, project_root)

    if not args.skip_build:
        run([args.cargo, "build", "--release", "--locked"], project_root)
    if not elf.is_file() or elf.stat().st_size == 0:
        raise RuntimeError(f"firmware ELF is missing or empty: {elf}")

    output_dir.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".radr-buttplug-bundle-", dir=output_dir.parent
    ) as temporary_directory:
        temporary = Path(temporary_directory)
        factory_image = temporary / FACTORY_IMAGE_NAME
        app_image = temporary / APP_IMAGE_NAME

        run(
            espflash_image_command(args.espflash, elf, app_image, merge=False),
            project_root,
        )
        run(
            espflash_image_command(args.espflash, elf, factory_image, merge=True),
            project_root,
        )
        validate_bundle_images(factory_image, app_image, partitions)

        manifest_path = temporary / MANIFEST_NAME
        write_json(manifest_path, web_manifest(version))

        binary_artifacts = {
            path.name: {"bytes": path.stat().st_size, "sha256": sha256(path)}
            for path in (factory_image, app_image)
        }
        metadata_path = temporary / BUNDLE_METADATA_NAME
        write_json(
            metadata_path,
            {
                "schema_version": 1,
                "name": "RADR Upstream Buttplug Rust Probe",
                "version": version,
                "repository_revision": git_revision(project_root),
                "upstream_buttplug_revision": upstream_revision,
                "target": {
                    "chip_family": "ESP32-S3",
                    "flash_bytes": FLASH_CAPACITY,
                    "flash_mode": "dio",
                    "flash_frequency_mhz": 80,
                    "application_partition": APP_PARTITION,
                },
                "partitions": [asdict(partition) for partition in partitions],
                "artifacts": binary_artifacts,
            },
        )

        install_path = temporary / INSTALL_PAGE_NAME
        install_path.write_text(install_page(), encoding="utf-8")

        checksum_targets = sorted(
            (factory_image, app_image, manifest_path, metadata_path, install_path),
            key=lambda path: path.name,
        )
        checksums_path = temporary / CHECKSUMS_NAME
        checksums_path.write_text(
            "".join(f"{sha256(path)}  {path.name}\n" for path in checksum_targets),
            encoding="utf-8",
        )

        output_dir.mkdir(parents=True, exist_ok=True)
        generated = [*checksum_targets, checksums_path]
        for source in generated:
            os.replace(source, output_dir / source.name)

    results = [output_dir / path.name for path in generated]
    print(f"Built validated install bundle in {output_dir}")
    for path in results:
        print(f"  {path.name}: {path.stat().st_size} bytes")
    return results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=PROJECT_ROOT)
    parser.add_argument("--output-dir", type=Path, default=PROJECT_ROOT / "dist")
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--espflash", default="espflash")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    try:
        build_bundle(args)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        print(f"Install bundle build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
