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
        MODULE_ONE_PACKAGE_DATA_FILES,
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
        MODULE_ONE_PACKAGE_DATA_FILES,
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
        "apps/Scripts/js_app.fap",
        "apps/Bluetooth/flipper_companion.fap",
        "apps/Tools/ai_dashboard.fap",
        "apps/Tools/flipper_relay.fap",
        "apps/Tools/quac.fap",
        "apps/Tools/tumoflip_packages.fap",
        "apps/Tools/totp.fap",
        "apps_data/js_app/plugins/js_gui.fal",
        "apps_data/js_app/plugins/js_subghz.fal",
    ):
        write_file(resources / relative, relative.encode())

    for relative in MODULE_ONE_PACKAGE_FILES:
        write_file(resources / relative, relative.encode())
    for relative in MODULE_ONE_PACKAGE_DATA_FILES:
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
            old_ir_lab = resources / "apps/Module One/IR Blaster/tumo_ir_lab.fap"
            extapp_ir_lab = build / ".extapps/tumo_ir_lab.fap"
            old_cockpit = resources / "apps/Module One/Diagnostics/cockpit.fap"
            extapp_cockpit = build / ".extapps/module_one_cockpit.fap"
            old_acceptance = resources / "apps/Module One/Diagnostics/tumo_acceptance_suite.fap"
            extapp_acceptance = build / ".extapps/tumo_acceptance_suite.fap"
            old_sensor_logger = resources / "apps/Module One/Sensors BME280/module_one_sensor_logger.fap"
            extapp_sensor_logger = build / ".extapps/module_one_sensor_logger.fap"
            old_ble_gatt_lab = resources / "apps/Module One/BLE/ble_gatt_lab.fap"
            extapp_ble_gatt_lab = build / ".extapps/ble_gatt_lab.fap"
            old_macro_deck = resources / "apps/Module One/Macros/tumo_macro_deck.fap"
            extapp_macro_deck = build / ".extapps/tumo_macro_deck.fap"
            old_wifi.write_bytes(b"old wifi mapper")
            write_file(extapp_wifi, b"wifi mapper fix")
            old_ir_lab.write_bytes(b"old ir lab")
            write_file(extapp_ir_lab, b"tumo ir lab fix")
            old_cockpit.write_bytes(b"old module one cockpit")
            write_file(extapp_cockpit, b"module one cockpit")
            old_acceptance.write_bytes(b"old acceptance")
            write_file(extapp_acceptance, b"acceptance suite")
            old_sensor_logger.write_bytes(b"old sensor logger")
            write_file(extapp_sensor_logger, b"sensor logger")
            old_ble_gatt_lab.write_bytes(b"old ble gatt lab")
            write_file(extapp_ble_gatt_lab, b"ble gatt lab")
            old_macro_deck.write_bytes(b"old macro deck")
            write_file(extapp_macro_deck, b"macro deck")

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
            self.assertEqual(old_ir_lab.read_bytes(), b"tumo ir lab fix")
            self.assertEqual(old_cockpit.read_bytes(), b"module one cockpit")
            self.assertEqual(old_acceptance.read_bytes(), b"acceptance suite")
            self.assertEqual(old_sensor_logger.read_bytes(), b"sensor logger")
            self.assertEqual(old_ble_gatt_lab.read_bytes(), b"ble gatt lab")
            self.assertEqual(old_macro_deck.read_bytes(), b"macro deck")

            module_entries = {
                entry["source"]: entry
                for entry in manifest["packages"]["module_one"]
            }
            base_entries = {
                entry["source"]: entry
                for entry in manifest["packages"]["base"]
            }
            self.assertIn("apps/Scripts/js_app.fap", base_entries)
            self.assertIn("apps_data/js_app/plugins/js_gui.fal", base_entries)
            self.assertIn("apps_data/js_app/plugins/js_subghz.fal", base_entries)
            wifi_entry = module_entries["apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"]
            self.assertEqual(wifi_entry["sha256"], sha256(extapp_wifi))
            ir_lab_entry = module_entries["apps/Module One/IR Blaster/tumo_ir_lab.fap"]
            self.assertEqual(ir_lab_entry["sha256"], sha256(extapp_ir_lab))
            cockpit_entry = module_entries[
                "apps/Module One/Diagnostics/cockpit.fap"
            ]
            self.assertEqual(cockpit_entry["sha256"], sha256(extapp_cockpit))
            acceptance_entry = module_entries[
                "apps/Module One/Diagnostics/tumo_acceptance_suite.fap"
            ]
            self.assertEqual(acceptance_entry["sha256"], sha256(extapp_acceptance))
            sensor_logger_entry = module_entries[
                "apps/Module One/Sensors BME280/module_one_sensor_logger.fap"
            ]
            self.assertEqual(sensor_logger_entry["sha256"], sha256(extapp_sensor_logger))
            ble_gatt_lab_entry = module_entries["apps/Module One/BLE/ble_gatt_lab.fap"]
            self.assertEqual(ble_gatt_lab_entry["sha256"], sha256(extapp_ble_gatt_lab))
            macro_deck_entry = module_entries["apps/Module One/Macros/tumo_macro_deck.fap"]
            self.assertEqual(macro_deck_entry["sha256"], sha256(extapp_macro_deck))
            self.assertIn(
                "apps_data/tumo_macro_deck/macros/safe_demo.tmacro",
                module_entries,
            )
            self.assertEqual(manifest["artifacts"], {})
            self.assertIn(
                {
                    "group": "module_one",
                    "legacy": "/ext/apps/Module One/module_one_cockpit.fap",
                    "canonical": "/ext/apps/Module One/Diagnostics/cockpit.fap",
                },
                manifest["cleanup"],
            )
            self.assertIn(
                {
                    "group": "module_one",
                    "legacy": "/ext/apps/Module One/tumo_acceptance_suite.fap",
                    "canonical": "/ext/apps/Module One/Diagnostics/tumo_acceptance_suite.fap",
                },
                manifest["cleanup"],
            )
            self.assertIn(
                {
                    "group": "module_one",
                    "legacy": "/ext/apps/Module One/module_one_sensor_logger.fap",
                    "canonical": "/ext/apps/Module One/Sensors BME280/module_one_sensor_logger.fap",
                },
                manifest["cleanup"],
            )

            manifest_path = (
                repo
                / "dist/f7-C/f7-update-tmwhflpprarf089-031/tumoflip-packages.json"
            )
            load_manifest(manifest_path)
            with zipfile.ZipFile(manifest_path.with_suffix(".zip")) as archive:
                self.assertIn(
                    "apps/Module One/IR Blaster/tumo_ir_lab.fap",
                    archive.namelist(),
                )
                self.assertIn("apps/Scripts/js_app.fap", archive.namelist())
                self.assertIn(
                    "apps_data/js_app/plugins/js_gui.fal",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps_data/js_app/plugins/js_subghz.fal",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/Diagnostics/cockpit.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/Diagnostics/tumo_acceptance_suite.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/Sensors BME280/module_one_sensor_logger.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/BLE/ble_gatt_lab.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/Macros/tumo_macro_deck.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps_data/tumo_macro_deck/macros/safe_demo.tmacro",
                    archive.namelist(),
                )
                self.assertEqual(
                    archive.read("apps/Module One/IR Blaster/tumo_ir_lab.fap"),
                    b"tumo ir lab fix",
                )
                self.assertEqual(
                    archive.read("apps/Module One/Diagnostics/cockpit.fap"),
                    b"module one cockpit",
                )
                self.assertEqual(
                    archive.read("apps/Module One/Diagnostics/tumo_acceptance_suite.fap"),
                    b"acceptance suite",
                )
                self.assertEqual(
                    archive.read("apps/Module One/Sensors BME280/module_one_sensor_logger.fap"),
                    b"sensor logger",
                )
                self.assertEqual(
                    archive.read("apps/Module One/BLE/ble_gatt_lab.fap"),
                    b"ble gatt lab",
                )
                self.assertEqual(
                    archive.read("apps/Module One/Macros/tumo_macro_deck.fap"),
                    b"macro deck",
                )
                self.assertEqual(
                    archive.read("apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"),
                    b"wifi mapper fix",
                )

    def test_package_only_release_routes_classic_rolljam_to_arf_module(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            legacy_visible = resources / "apps/ARF Tools/rolljam_standalone.fap"
            extapp_rolljam = build / ".extapps/rolljam.fap"
            extapp_standalone = build / ".extapps/rolljam_standalone.fap"
            write_file(legacy_visible, b"old visible rolljam")
            write_file(extapp_rolljam, b"classic rolljam module")
            write_file(extapp_standalone, b"shield receiver module")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-tmwhflpprarf089-031",
                package_id="rolljam-module",
                target_release_tag="v0.3.1",
            )

            canonical = resources / "apps_data/arf_subghz_full/modules/rolljam.fap"
            self.assertFalse(legacy_visible.exists())
            self.assertEqual(canonical.read_bytes(), b"classic rolljam module")

            synced_sources = {
                entry["source"] for entry in manifest["package_release"]["synced_extapps"]
            }
            self.assertIn(".extapps/rolljam.fap", synced_sources)
            self.assertNotIn(".extapps/rolljam_standalone.fap", synced_sources)

            arf_entries = {entry["target"]: entry for entry in manifest["packages"]["arf"]}
            self.assertIn(
                "/ext/apps_data/arf_subghz_full/modules/rolljam.fap", arf_entries
            )
            self.assertNotIn("/ext/apps/ARF Tools/rolljam_standalone.fap", arf_entries)
            self.assertIn(
                {
                    "group": "arf",
                    "legacy": "/ext/apps/ARF Tools/rolljam_standalone.fap",
                    "canonical": "/ext/apps_data/arf_subghz_full/modules/rolljam.fap",
                },
                manifest["cleanup"],
            )

    def test_package_only_release_removes_retired_ble_killer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            stale_visible = resources / "apps/ARF Tools/ble_killer.fap"
            stale_extapp = build / ".extapps/ble_killer.fap"
            write_file(stale_visible, b"old ble killer")
            write_file(stale_extapp, b"stale ble killer extapp")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-tmwhflpprarf089-031",
                package_id="remove-ble-killer",
                target_release_tag="v0.3.1",
            )

            self.assertFalse(stale_visible.exists())
            synced_sources = {
                entry["source"] for entry in manifest["package_release"]["synced_extapps"]
            }
            self.assertNotIn(".extapps/ble_killer.fap", synced_sources)

            arf_entries = {entry["target"]: entry for entry in manifest["packages"]["arf"]}
            self.assertNotIn("/ext/apps/ARF Tools/ble_killer.fap", arf_entries)
            self.assertIn(
                {
                    "group": "arf",
                    "legacy": "/ext/apps/ARF Tools/ble_killer.fap",
                    "canonical": "/ext/apps/ARF Tools/arf_subghz_full.fap",
                },
                manifest["cleanup"],
            )

    def test_package_only_release_removes_module_one_frequency_analyzer_copy(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            stale_visible = resources / "apps/Module One/Sub-GHz/freq_analyzer_ext.fap"
            write_file(stale_visible, b"old module one analyzer")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-tmwhflpprarf089-031",
                package_id="remove-module-one-analyzer",
                target_release_tag="v0.3.1",
            )

            self.assertFalse(stale_visible.exists())
            arf_entries = {entry["target"]: entry for entry in manifest["packages"]["arf"]}
            self.assertIn("/ext/apps/ARF Tools/arf_frequency_analyzer.fap", arf_entries)
            self.assertNotIn(
                "/ext/apps/Module One/Sub-GHz/freq_analyzer_ext.fap",
                arf_entries,
            )
            self.assertIn(
                {
                    "group": "arf",
                    "legacy": "/ext/apps/Module One/Sub-GHz/freq_analyzer_ext.fap",
                    "canonical": "/ext/apps/ARF Tools/arf_frequency_analyzer.fap",
                },
                manifest["cleanup"],
            )


if __name__ == "__main__":
    unittest.main()
