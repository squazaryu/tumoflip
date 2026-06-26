#!/usr/bin/env python3

import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

try:
    from .apply_packages import load_manifest
    from .package_release import build_package_release
    from .validate_release import (
        ARF_MODULE_APP_IDS,
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        sha256,
    )
except ImportError:
    from apply_packages import load_manifest
    from package_release import build_package_release
    from validate_release import (
        ARF_MODULE_APP_IDS,
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        sha256,
    )


def write_file(path: Path, data: bytes = b"tumoflip") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def prepare_package_tree(root: Path) -> tuple[Path, Path, Path]:
    repo = root / "repo"
    build = repo / "build/f7-firmware-C"
    resources = build / "resources"
    (repo / "targets/f7").mkdir(parents=True)
    (repo / "targets/f7/api_symbols.csv").write_text(
        "Version,+,88.0,\n", encoding="utf-8"
    )
    build.mkdir(parents=True)
    (build / "firmware.json").write_text(
        json.dumps(
            {
                "firmware_target": 7,
                "firmware_version": "tmwhflpprarf089-031",
            }
        ),
        encoding="utf-8",
    )
    write_file(resources / "Manifest")

    for relative in (
        "apps/Bluetooth/flipper_companion.fap",
        "apps/Tools/ai_dashboard.fap",
        "apps/Tools/flipper_relay.fap",
        "apps/Tools/quac.fap",
        "apps/Tools/totp.fap",
    ):
        write_file(resources / relative, relative.encode())

    for relative in MODULE_ONE_PACKAGE_FILES:
        write_file(resources / relative, relative.encode())

    for appid in ARF_VISIBLE_APP_IDS:
        write_file(resources / f"apps/ARF Tools/{appid}.fap", appid.encode())
    for appid in ARF_MODULE_APP_IDS:
        write_file(
            resources / f"apps_data/arf_subghz_full/modules/{appid}.fap",
            appid.encode(),
        )
    for protocol in PROTOCOL_PACKS:
        write_file(resources / f"apps_data/subghz/plugins/{protocol}", protocol.encode())

    subprocess.run(["git", "init"], cwd=repo, check=True, capture_output=True)
    subprocess.run(["git", "config", "user.email", "test@example.invalid"], cwd=repo)
    subprocess.run(["git", "config", "user.name", "Tumoflip Test"], cwd=repo)
    subprocess.run(["git", "add", "."], cwd=repo, check=True, capture_output=True)
    subprocess.run(
        ["git", "commit", "-m", "test package release"],
        cwd=repo,
        check=True,
        capture_output=True,
    )

    return repo, build, resources


class PackageReleaseTest(unittest.TestCase):
    def test_package_only_manifest_uses_current_extapp_wifi_mapper(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            old_wifi = resources / "apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"
            extapp_wifi = build / ".extapps/wifi_mapper.fap"
            old_wifi.write_bytes(b"old wifi mapper")
            write_file(extapp_wifi, b"wifi mapper fix")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-tmwhflpprarf089-031",
                package_id="wifi-mapper-fix",
                target_release_tag="v0.3.1",
            )

            self.assertEqual(manifest["package_release"]["type"], "package-only")
            self.assertEqual(manifest["package_release"]["id"], "wifi-mapper-fix")
            self.assertTrue(manifest["package_release"]["firmware_flash_unchanged"])
            self.assertEqual(
                old_wifi.read_bytes(),
                b"wifi mapper fix",
            )

            module_entries = {
                entry["source"]: entry
                for entry in manifest["packages"]["module_one"]
            }
            wifi_entry = module_entries["apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"]
            self.assertEqual(wifi_entry["sha256"], sha256(extapp_wifi))
            self.assertEqual(manifest["artifacts"], {})

            manifest_path = (
                repo
                / "dist/f7-C/f7-update-tmwhflpprarf089-031/tumoflip-packages.json"
            )
            load_manifest(manifest_path)
            with zipfile.ZipFile(manifest_path.with_suffix(".zip")) as archive:
                self.assertIn(
                    "apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
                    archive.namelist(),
                )
                self.assertEqual(
                    archive.read("apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"),
                    b"wifi mapper fix",
                )


if __name__ == "__main__":
    unittest.main()
