import argparse
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import publish_immutable_firmware as publisher


parse_artifact = publisher.parse_artifact
read_version = publisher.read_version


class PublisherTests(unittest.TestCase):
    def test_reads_numeric_semantic_version(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Version.h"
            path.write_text('#define VERSION "1.2.3"\n')
            self.assertEqual(read_version(path), "1.2.3")

    def test_rejects_missing_artifact(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            parse_artifact("application:missing.bin:1:true")

    def test_parses_installable_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(b"firmware")
            artifact = parse_artifact(f"application:{path}:2:true")
            self.assertTrue(artifact.installable)
            self.assertEqual(artifact.install_order, 2)
            self.assertEqual(artifact.sha256, "c3bf47ea1f4a4a605470313cacb3a44f4a461f68c6faeab07e737610cb5ac835")

    def test_parses_non_installable_web_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "web-installer.bin"
            path.write_bytes(b"merged-factory-image")
            artifact = parse_artifact(f"web-installer:{path}:5:false")
            self.assertEqual(artifact.role, "web-installer")
            self.assertFalse(artifact.installable)

    def test_rejects_installable_web_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "web-installer.bin"
            path.write_bytes(b"merged-factory-image")
            with self.assertRaisesRegex(
                argparse.ArgumentTypeError, "must be non-installable"
            ):
                parse_artifact(f"web-installer:{path}:5:true")

    def test_release_includes_web_installer_as_optional(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version_file = root / "Version.h"
            version_file.write_text('#define VERSION "1.2.3"\n', encoding="utf-8")
            artifact_specs = (
                ("filesystem", "littlefs.bin", 1, True),
                ("application", "firmware.bin", 2, True),
                ("bootloader", "bootloader.bin", 3, False),
                ("partitions", "partitions.bin", 4, False),
                ("web-installer", "web-installer.bin", 5, False),
            )
            artifacts = []
            for role, filename, order, installable in artifact_specs:
                path = root / filename
                path.write_bytes(filename.encode())
                artifacts.append(publisher.Artifact(role, path, order, installable))
            args = argparse.Namespace(
                device_type="radr",
                track="staging",
                build_sha="abcdef0",
                version_file=version_file,
                kind="firmware",
                artifact=artifacts,
            )
            upload_payload = {}
            release_payload = {}

            def request_json(url, _token, payload):
                if url.endswith("/uploads"):
                    upload_payload.update(payload)
                    return {
                        "objectPrefix": "releases/1.2.3/abcdef0",
                        "uploads": [
                            {
                                "filename": artifact["filename"],
                                "signedUrl": f"https://upload.invalid/{artifact['filename']}",
                                "publicUrl": f"https://public.invalid/{artifact['filename']}",
                                "objectPath": f"releases/1.2.3/abcdef0/{artifact['filename']}",
                            }
                            for artifact in payload["artifacts"]
                        ],
                    }
                if url.endswith("/releases"):
                    release_payload.update(payload)
                    return {"releaseId": "release-id"}
                return {}

            with (
                mock.patch.dict(
                    os.environ,
                    {
                        "FIRMWARE_PUBLISH_TOKEN": "test-token",
                        "RUNNER_TEMP": str(root / "runner-temp"),
                    },
                    clear=True,
                ),
                mock.patch.object(publisher, "request_json", side_effect=request_json),
                mock.patch.object(publisher, "upload_file"),
                mock.patch.object(publisher, "verify_public_object"),
            ):
                self.assertEqual(publisher.publish(args), "release-id")

            self.assertIn(
                "web-installer",
                [artifact["role"] for artifact in upload_payload["artifacts"]],
            )
            self.assertEqual(
                [artifact["role"] for artifact in release_payload["artifacts"]],
                ["filesystem", "application", "web-installer"],
            )
            self.assertEqual(
                [artifact["required"] for artifact in release_payload["artifacts"]],
                [True, True, False],
            )

    def test_publish_checks_web_installer_non_installable_invariant_first(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version_file = root / "Version.h"
            version_file.write_text('#define VERSION "1.2.3"\n', encoding="utf-8")
            application = root / "firmware.bin"
            application.write_bytes(b"firmware")
            web_installer = root / "web-installer.bin"
            web_installer.write_bytes(b"merged")
            args = argparse.Namespace(
                device_type="radr",
                track="staging",
                build_sha="abcdef0",
                version_file=version_file,
                kind="firmware",
                artifact=[
                    publisher.Artifact("application", application, 1, True),
                    publisher.Artifact("web-installer", web_installer, 2, True),
                ],
            )
            with (
                mock.patch.dict(
                    os.environ, {"FIRMWARE_PUBLISH_TOKEN": "test-token"}, clear=True
                ),
                self.assertRaisesRegex(RuntimeError, "must be non-installable"),
            ):
                publisher.publish(args)


if __name__ == "__main__":
    unittest.main()
