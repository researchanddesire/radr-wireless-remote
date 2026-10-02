#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("build_install_bundle.py")
SPEC = importlib.util.spec_from_file_location("build_install_bundle", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def image_header() -> bytes:
    header = bytearray(32)
    header[0] = MODULE.ESP_IMAGE_MAGIC
    header[2] = MODULE.FLASH_MODE_DIO_ID
    header[3] = (
        MODULE.FLASH_SIZE_16MB_ID << 4 | MODULE.FLASH_FREQUENCY_80MHZ_ID
    )
    header[12:14] = MODULE.ESP32S3_CHIP_ID.to_bytes(2, "little")
    return bytes(header)


def binary_partition(partition: object) -> bytes:
    name = partition.name.encode("utf-8").ljust(16, b"\0")
    return struct.pack(
        "<HBBII16sI",
        MODULE.PARTITION_MAGIC,
        partition.type_id,
        partition.subtype_id,
        partition.offset,
        partition.size,
        name,
        partition.flags,
    )


class BuildInstallBundleTests(unittest.TestCase):
    def test_partition_numbers_accept_hex_and_suffixes(self) -> None:
        self.assertEqual(MODULE.parse_number("0x10000"), 0x10000)
        self.assertEqual(MODULE.parse_number("64K"), 64 * 1024)
        self.assertEqual(MODULE.parse_number("16M"), 16 * 1024 * 1024)

    def test_project_partition_table_has_expected_dual_ota_layout(self) -> None:
        partitions = MODULE.read_partition_table(MODULE.PROJECT_ROOT / "partitions.csv")
        by_name = {partition.name: partition for partition in partitions}
        self.assertEqual(by_name["app0"].offset, 0x10000)
        self.assertEqual(by_name["app0"].size, 0x640000)
        self.assertEqual(by_name["app1"].offset, 0x650000)
        self.assertEqual(by_name["app1"].size, 0x640000)
        self.assertEqual(by_name["spiffs"].offset, 0xC90000)

    def test_project_metadata_uses_one_official_revision(self) -> None:
        version, revision = MODULE.project_metadata(MODULE.PROJECT_ROOT)
        self.assertEqual(version, "0.1.0")
        self.assertEqual(revision, "9571b3db42ee2d7b3342ab9d40eb5c9e45679444")

    def test_web_manifest_installs_one_merged_esp32s3_image(self) -> None:
        manifest = MODULE.web_manifest("0.1.0")
        self.assertTrue(manifest["new_install_prompt_erase"])
        build = manifest["builds"][0]
        self.assertEqual(build["chipFamily"], "ESP32-S3")
        self.assertEqual(
            build["parts"], [{"path": MODULE.FACTORY_IMAGE_NAME, "offset": 0}]
        )

    def test_validates_exact_partition_and_application_content(self) -> None:
        partitions = [
            MODULE.Partition("nvs", 1, 0x02, 0x9000, 0x100),
            MODULE.Partition("otadata", 1, 0x00, 0xE000, 0x100),
            MODULE.Partition("app0", 0, 0x10, 0x10000, 0x1000),
            MODULE.Partition("app1", 0, 0x11, 0x11000, 0x1000),
        ]
        app_partition = partitions[2]
        app = image_header() + b"application"
        factory = bytearray(b"\xff" * (app_partition.offset + len(app)))
        factory[: len(image_header())] = image_header()
        for index, partition in enumerate(partitions):
            offset = MODULE.PARTITION_TABLE_OFFSET + index * 32
            factory[offset : offset + 32] = binary_partition(partition)
        factory[app_partition.offset :] = app

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            factory_path = root / MODULE.FACTORY_IMAGE_NAME
            app_path = root / MODULE.APP_IMAGE_NAME
            factory_path.write_bytes(factory)
            app_path.write_bytes(app)
            MODULE.validate_bundle_images(factory_path, app_path, partitions)

            factory[-1] ^= 0xFF
            factory_path.write_bytes(factory)
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                MODULE.validate_bundle_images(factory_path, app_path, partitions)

            factory[-1] ^= 0xFF
            factory[partitions[0].offset] = 0
            factory_path.write_bytes(factory)
            with self.assertRaisesRegex(RuntimeError, "not erased"):
                MODULE.validate_bundle_images(factory_path, app_path, partitions)

    def test_rejects_wrong_flash_header(self) -> None:
        image = bytearray(image_header())
        image[2] = 0
        with self.assertRaisesRegex(RuntimeError, "DIO"):
            MODULE.validate_image_header(image, 0, "test")


if __name__ == "__main__":
    unittest.main()
