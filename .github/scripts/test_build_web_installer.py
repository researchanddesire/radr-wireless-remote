#!/usr/bin/env python3

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("build_web_installer.py")
SPEC = importlib.util.spec_from_file_location("build_web_installer", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class BuildWebInstallerTests(unittest.TestCase):
    def test_profiles_use_platform_offsets_and_physical_capacity(self):
        self.assertEqual(MODULE.PROFILES["esp32-4mb"].bootloader_offset, 0x1000)
        self.assertEqual(MODULE.PROFILES["esp32-16mb"].capacity, 16 * 1024 * 1024)
        self.assertEqual(MODULE.PROFILES["esp32s3-16mb"].bootloader_offset, 0)
        self.assertEqual(MODULE.PROFILES["esp32s3-16mb"].flash_mode, "dio")
        self.assertEqual(MODULE.PROFILES["esp32s3-16mb"].flash_freq, "80m")

    def test_partition_offset_is_read_from_csv(self):
        with tempfile.TemporaryDirectory() as directory:
            table = Path(directory) / "partitions.csv"
            table.write_text(
                "# Name,Type,SubType,Offset,Size\n"
                "littlefs,data,spiffs,0xc90000,0x1000\n",
                encoding="utf-8",
            )
            self.assertEqual(
                MODULE.partition_layout(table, "littlefs"),
                (0xC90000, 0x1000),
            )

    def test_partition_sizes_accept_platformio_suffixes(self):
        self.assertEqual(MODULE.parse_partition_number("64K"), 64 * 1024)
        self.assertEqual(MODULE.parse_partition_number("2M"), 2 * 1024 * 1024)

    def test_rejects_filesystem_larger_than_partition(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_dir = root / "build"
            build_dir.mkdir()
            for name, content in (
                ("bootloader.bin", b"\xe9boot"),
                ("partitions.bin", b"partition-table"),
                ("firmware.bin", b"\xe9firmware"),
            ):
                (build_dir / name).write_bytes(content)
            boot_app0 = root / "boot_app0.bin"
            boot_app0.write_bytes(b"boot-app")
            filesystem = root / "littlefs.bin"
            filesystem.write_bytes(b"too-large")

            with self.assertRaisesRegex(RuntimeError, "beyond its 4-byte partition"):
                MODULE.build_components(
                    MODULE.PROFILES["esp32s3-16mb"],
                    build_dir,
                    boot_app0,
                    filesystem,
                    0xC90000,
                    4,
                )

    def test_merge_command_uses_radr_flash_configuration(self):
        profile = MODULE.PROFILES["esp32s3-16mb"]
        command = MODULE.merge_command(
            profile, Path("merged.bin"), [], Path("esptool.py")
        )
        self.assertIn("esp32s3", command)
        self.assertEqual(command[command.index("--flash_mode") + 1], "keep")
        self.assertEqual(command[command.index("--flash_freq") + 1], "keep")
        self.assertEqual(command[command.index("--flash_size") + 1], "16MB")

    def test_validation_checks_magic_and_exact_components(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile = MODULE.PROFILES["esp32s3-16mb"]
            sources = {}
            bootloader = bytearray(64)
            bootloader[0] = 0xE9
            bootloader[1] = 1
            bootloader[2] = profile.flash_mode_id
            bootloader[3] = (profile.flash_size_id << 4) | profile.flash_freq_id
            bootloader[12:14] = profile.chip_id.to_bytes(2, "little")
            application = bytearray(24)
            application[0] = 0xE9
            application[1] = 1
            application[12:14] = profile.chip_id.to_bytes(2, "little")
            for offset, name, content in (
                (0x0, "bootloader.bin", bytes(bootloader)),
                (0x8000, "partitions.bin", b"partition-table"),
                (0xE000, "boot_app0.bin", b"boot-app"),
                (0x10000, "firmware.bin", bytes(application)),
            ):
                path = root / name
                path.write_bytes(content)
                sources[offset] = path
            merged = bytearray(b"\xff" * 0x10020)
            for offset, path in sources.items():
                content = path.read_bytes()
                merged[offset : offset + len(content)] = content
            output = root / "web-installer.bin"
            output.write_bytes(merged)

            MODULE.validate_merged_image(output, profile, sorted(sources.items()))

            merged[0x10000] = 0
            output.write_bytes(merged)
            with self.assertRaisesRegex(RuntimeError, "magic"):
                MODULE.validate_merged_image(output, profile, sorted(sources.items()))

            merged[0x10000] = 0xE9
            merged[0x10000 + 12] = 0
            output.write_bytes(merged)
            with self.assertRaisesRegex(RuntimeError, "chip ID"):
                MODULE.validate_merged_image(output, profile, sorted(sources.items()))

            merged[0x10000 + 12] = profile.chip_id
            merged[2] = 0
            output.write_bytes(merged)
            with self.assertRaisesRegex(RuntimeError, "flash mode"):
                MODULE.validate_merged_image(output, profile, sorted(sources.items()))


if __name__ == "__main__":
    unittest.main()
