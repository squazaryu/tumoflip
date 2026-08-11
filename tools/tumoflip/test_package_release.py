#!/usr/bin/env python3

import json
import os
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

try:
    from .apply_packages import load_manifest
    from .make_packages_zip import build_packages_zip
    from .package_release import build_package_release
    from .validate_release import (
        ARF_MODULE_APP_IDS,
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_DATA_FILES,
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        ValidationError,
        manifest_release_id,
        md5,
        package_entries,
        sha256,
    )
except ImportError:
    from apply_packages import load_manifest
    from make_packages_zip import build_packages_zip
    from package_release import build_package_release
    from validate_release import (
        ARF_MODULE_APP_IDS,
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_DATA_FILES,
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        ValidationError,
        manifest_release_id,
        md5,
        package_entries,
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
        "apps/Bluetooth/claude_buddy.fap",
        "apps/Bluetooth/flipper_companion.fap",
        "apps/Sub-GHz/subghz_wardriving.fap",
        "apps/Tools/ai_dashboard.fap",
        "apps/Tools/clock.fap",
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


def prepare_target_package(
    repo: Path,
    resources: Path,
    excluded_sources: set[str] | None = None,
) -> tuple[dict[str, object], Path]:
    excluded = excluded_sources or set()
    packages = package_entries(resources)
    packages = {
        group: [entry for entry in entries if entry["source"] not in excluded]
        for group, entries in packages.items()
    }
    manifest: dict[str, object] = {
        "schema": 2,
        "firmware": {
            "name": "tumoflip",
            "version": "t-flppr-fw-004",
            "target": 7,
            "api": "88.0",
            "radio_address": "0x080D7000",
        },
        "safety": {"section_gap_bytes": 16384},
        "artifacts": {"stable.tgz": {"sha256": "a" * 64, "bytes": 123}},
        "packages": packages,
        "cleanup": [],
    }
    manifest["release_id"] = manifest_release_id(manifest)
    package_zip = repo / "target-release/tumoflip-packages.zip"
    build_packages_zip(manifest, resources, package_zip)
    return manifest, package_zip


class PackageReleaseTest(unittest.TestCase):
    def test_package_release_can_target_existing_stable_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-t-flppr-fw-004",
                target_release_tag="v1.0.4",
                target_manifest=target_manifest,
                target_package_zip=target_zip,
            )

            self.assertEqual(manifest["firmware"], target_manifest["firmware"])
            self.assertEqual(
                manifest["package_release"]["source_firmware_version"],
                "tmwhflpprarf089-031",
            )
            self.assertEqual(
                manifest["package_release"]["target_release_id"],
                target_manifest["release_id"],
            )
            self.assertTrue(
                manifest["package_release"]["firmware_flash_unchanged"]
            )

    def test_package_release_rejects_target_with_different_api(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            target_manifest["firmware"]["api"] = "87.0"
            target_manifest.pop("release_id")
            target_manifest["release_id"] = manifest_release_id(target_manifest)

            with self.assertRaisesRegex(
                ValidationError,
                "does not match package build API",
            ):
                build_package_release(
                    repo,
                    build,
                    repo / "dist/f7-C/f7-update-t-flppr-fw-004",
                    target_release_tag="v1.0.4",
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                )

    def test_existing_stable_release_overlays_only_esp_flasher(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            flasher_source = "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap"
            target_manifest, target_zip = prepare_target_package(
                repo,
                resources,
                excluded_sources={flasher_source},
            )
            old_wifi = resources / "apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"
            old_wifi_bytes = old_wifi.read_bytes()
            write_file(build / ".extapps/wifi_mapper.fap", b"unaccepted dev wifi mapper")
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-t-flppr-fw-004",
                target_release_tag="v1.0.4",
                target_manifest=target_manifest,
                target_package_zip=target_zip,
            )

            def by_target(value: dict[str, object]) -> dict[str, dict[str, object]]:
                return {
                    entry["target"]: entry
                    for entries in value["packages"].values()
                    for entry in entries
                }

            old_entries = by_target(target_manifest)
            new_entries = by_target(manifest)
            flasher_target = f"/ext/{flasher_source}"
            self.assertEqual(
                {key: value for key, value in new_entries.items() if key != flasher_target},
                old_entries,
            )
            self.assertEqual(
                set(new_entries) - set(old_entries),
                {flasher_target},
            )
            self.assertEqual(manifest["artifacts"], target_manifest["artifacts"])
            self.assertEqual(manifest["safety"], target_manifest["safety"])
            self.assertEqual(
                manifest["package_release"]["overlay_targets"],
                [flasher_source],
            )

            output_zip = repo / "dist/f7-C/f7-update-t-flppr-fw-004/tumoflip-packages.zip"
            with zipfile.ZipFile(target_zip) as old_archive, zipfile.ZipFile(output_zip) as new_archive:
                for name in old_archive.namelist():
                    self.assertEqual(new_archive.read(name), old_archive.read(name))
                self.assertEqual(
                    new_archive.read(flasher_source),
                    b"protected esp flasher",
                )
                self.assertEqual(new_archive.read(old_wifi.relative_to(resources).as_posix()), old_wifi_bytes)

    def test_existing_stable_release_rejects_corrupt_package_zip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")
            corrupt_zip = repo / "target-release/corrupt.zip"
            with zipfile.ZipFile(target_zip) as source, zipfile.ZipFile(corrupt_zip, "w") as output:
                for info in source.infolist():
                    data = source.read(info)
                    if info.filename == source.infolist()[0].filename:
                        data = bytes([data[0] ^ 0xFF]) + data[1:]
                    output.writestr(info, data)

            with self.assertRaisesRegex(ValidationError, "digest differs"):
                build_package_release(
                    repo,
                    build,
                    repo / "dist/f7-C/f7-update-t-flppr-fw-004",
                    target_release_tag="v1.0.4",
                    target_manifest=target_manifest,
                    target_package_zip=corrupt_zip,
                )

    def test_existing_stable_release_rejects_extra_zip_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")
            with zipfile.ZipFile(target_zip, "a") as archive:
                archive.writestr("../escape.fap", b"escape")

            with self.assertRaisesRegex(ValidationError, "contents differ"):
                build_package_release(
                    repo,
                    build,
                    repo / "dist/f7-C/f7-update-t-flppr-fw-004",
                    target_release_tag="v1.0.4",
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                )

    def test_existing_stable_release_rejects_stale_release_id(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            target_manifest["artifacts"]["stable.tgz"]["bytes"] = 124
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")

            with self.assertRaisesRegex(ValidationError, "release ID differs"):
                build_package_release(
                    repo,
                    build,
                    repo / "dist/f7-C/f7-update-t-flppr-fw-004",
                    target_release_tag="v1.0.4",
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                )

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
            old_trace_viewer = resources / "apps/Module One/Diagnostics/runtime_trace_viewer.fap"
            extapp_trace_viewer = build / ".extapps/runtime_trace_viewer.fap"
            old_field_logger = resources / "apps/Module One/Field/field_logger.fap"
            extapp_field_logger = build / ".extapps/field_logger.fap"
            old_signal_workbench = resources / "apps/Module One/Signals/signal_workbench.fap"
            extapp_signal_workbench = build / ".extapps/signal_workbench.fap"
            old_sensor_logger = resources / "apps/Module One/Sensors BME280/module_one_sensor_logger.fap"
            extapp_sensor_logger = build / ".extapps/module_one_sensor_logger.fap"
            old_ble_gatt_lab = resources / "apps/Module One/BLE/ble_gatt_lab.fap"
            extapp_ble_gatt_lab = build / ".extapps/ble_gatt_lab.fap"
            old_app_bridge_terminal = resources / "apps/Module One/BLE/app_bridge_terminal.fap"
            extapp_app_bridge_terminal = build / ".extapps/app_bridge_terminal.fap"
            old_macro_deck = resources / "apps/Module One/Macros/tumo_macro_deck.fap"
            extapp_macro_deck = build / ".extapps/tumo_macro_deck.fap"
            old_tumoscript = resources / "apps/Module One/Scripts/tumoscript.fap"
            extapp_tumoscript = build / ".extapps/tumoscript.fap"
            old_wifi.write_bytes(b"old wifi mapper")
            write_file(extapp_wifi, b"wifi mapper fix")
            old_ir_lab.write_bytes(b"old ir lab")
            write_file(extapp_ir_lab, b"tumo ir lab fix")
            old_cockpit.write_bytes(b"old module one cockpit")
            write_file(extapp_cockpit, b"module one cockpit")
            old_acceptance.write_bytes(b"old acceptance")
            write_file(extapp_acceptance, b"acceptance suite")
            old_trace_viewer.write_bytes(b"old trace viewer")
            write_file(extapp_trace_viewer, b"runtime trace viewer")
            old_field_logger.write_bytes(b"old field logger")
            write_file(extapp_field_logger, b"field logger")
            old_signal_workbench.write_bytes(b"old signal workbench")
            write_file(extapp_signal_workbench, b"signal workbench")
            old_sensor_logger.write_bytes(b"old sensor logger")
            write_file(extapp_sensor_logger, b"sensor logger")
            old_ble_gatt_lab.write_bytes(b"old ble gatt lab")
            write_file(extapp_ble_gatt_lab, b"ble gatt lab")
            old_app_bridge_terminal.write_bytes(b"old app bridge terminal")
            write_file(extapp_app_bridge_terminal, b"app bridge terminal")
            old_macro_deck.write_bytes(b"old macro deck")
            write_file(extapp_macro_deck, b"macro deck")
            old_tumoscript.write_bytes(b"old tumoscript")
            write_file(extapp_tumoscript, b"tumoscript")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-tmwhflpprarf089-031",
                package_id="wifi-mapper-fix",
                target_release_tag="v0.3.1",
            )
            package_zip = (
                repo
                / "dist/f7-C/f7-update-tmwhflpprarf089-031/tumoflip-packages.zip"
            )
            first_zip = package_zip.read_bytes()
            for index, path in enumerate(sorted(resources.rglob("*"))):
                if path.is_file():
                    timestamp = 1_700_000_000 + index
                    os.utime(path, (timestamp, timestamp))
            repeated_manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-tmwhflpprarf089-031",
                package_id="wifi-mapper-fix",
                target_release_tag="v0.3.1",
            )

            self.assertEqual(manifest["release_id"], repeated_manifest["release_id"])
            self.assertEqual(first_zip, package_zip.read_bytes())

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
            self.assertEqual(old_trace_viewer.read_bytes(), b"runtime trace viewer")
            self.assertEqual(old_field_logger.read_bytes(), b"field logger")
            self.assertEqual(old_signal_workbench.read_bytes(), b"signal workbench")
            self.assertEqual(old_sensor_logger.read_bytes(), b"sensor logger")
            self.assertEqual(old_ble_gatt_lab.read_bytes(), b"ble gatt lab")
            self.assertEqual(old_app_bridge_terminal.read_bytes(), b"app bridge terminal")
            self.assertEqual(old_macro_deck.read_bytes(), b"macro deck")
            self.assertEqual(old_tumoscript.read_bytes(), b"tumoscript")

            module_entries = {
                entry["source"]: entry
                for entry in manifest["packages"]["module_one"]
            }
            base_entries = {
                entry["source"]: entry
                for entry in manifest["packages"]["base"]
            }
            self.assertNotIn("apps/Scripts/js_app.fap", base_entries)
            self.assertIn("apps/Bluetooth/claude_buddy.fap", base_entries)
            self.assertIn("apps/Sub-GHz/subghz_wardriving.fap", base_entries)
            self.assertNotIn("apps_data/js_app/plugins/js_gui.fal", base_entries)
            self.assertNotIn("apps_data/js_app/plugins/js_subghz.fal", base_entries)
            self.assertFalse((resources / "apps/Scripts/js_app.fap").exists())
            wifi_entry = module_entries["apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"]
            self.assertEqual(wifi_entry["sha256"], sha256(extapp_wifi))
            self.assertEqual(wifi_entry["md5"], md5(extapp_wifi))
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
            trace_viewer_entry = module_entries[
                "apps/Module One/Diagnostics/runtime_trace_viewer.fap"
            ]
            self.assertEqual(trace_viewer_entry["sha256"], sha256(extapp_trace_viewer))
            field_logger_entry = module_entries["apps/Module One/Field/field_logger.fap"]
            self.assertEqual(field_logger_entry["sha256"], sha256(extapp_field_logger))
            signal_workbench_entry = module_entries[
                "apps/Module One/Signals/signal_workbench.fap"
            ]
            self.assertEqual(signal_workbench_entry["sha256"], sha256(extapp_signal_workbench))
            sensor_logger_entry = module_entries[
                "apps/Module One/Sensors BME280/module_one_sensor_logger.fap"
            ]
            self.assertEqual(sensor_logger_entry["sha256"], sha256(extapp_sensor_logger))
            ble_gatt_lab_entry = module_entries["apps/Module One/BLE/ble_gatt_lab.fap"]
            self.assertEqual(ble_gatt_lab_entry["sha256"], sha256(extapp_ble_gatt_lab))
            app_bridge_terminal_entry = module_entries[
                "apps/Module One/BLE/app_bridge_terminal.fap"
            ]
            self.assertEqual(
                app_bridge_terminal_entry["sha256"], sha256(extapp_app_bridge_terminal)
            )
            macro_deck_entry = module_entries["apps/Module One/Macros/tumo_macro_deck.fap"]
            self.assertEqual(macro_deck_entry["sha256"], sha256(extapp_macro_deck))
            tumoscript_entry = module_entries["apps/Module One/Scripts/tumoscript.fap"]
            self.assertEqual(tumoscript_entry["sha256"], sha256(extapp_tumoscript))
            self.assertNotIn("apps/Module One/Security/tumokey.fap", module_entries)
            self.assertIn(
                "apps_data/tumo_macro_deck/macros/safe_demo.tmacro",
                module_entries,
            )
            self.assertIn(
                "apps_data/tumoscript/scripts/safe_demo.tscr",
                module_entries,
            )
            self.assertEqual(manifest["artifacts"], {})
            self.assertIn(
                {
                    "group": "base",
                    "legacy": "/ext/apps/Scripts/js_app.fap",
                    "canonical": "/ext/apps/Bluetooth/flipper_companion.fap",
                },
                manifest["cleanup"],
            )
            self.assertIn(
                {
                    "group": "module_one",
                    "legacy": "/ext/apps/Module One/Diagnostics/module_one_cockpit.fap",
                    "canonical": "/ext/apps/Module One/Diagnostics/cockpit.fap",
                },
                manifest["cleanup"],
            )
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
                self.assertNotIn("apps/Scripts/js_app.fap", archive.namelist())
                self.assertNotIn(
                    "apps_data/js_app/plugins/js_gui.fal",
                    archive.namelist(),
                )
                self.assertNotIn(
                    "apps_data/js_app/plugins/js_subghz.fal",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Sub-GHz/subghz_wardriving.fap",
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
                    "apps/Module One/Field/field_logger.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/Signals/signal_workbench.fap",
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
                    "apps/Module One/BLE/app_bridge_terminal.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/Macros/tumo_macro_deck.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps/Module One/Scripts/tumoscript.fap",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps_data/tumo_macro_deck/macros/safe_demo.tmacro",
                    archive.namelist(),
                )
                self.assertIn(
                    "apps_data/tumoscript/scripts/safe_demo.tscr",
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
                    archive.read("apps/Module One/Field/field_logger.fap"),
                    b"field logger",
                )
                self.assertEqual(
                    archive.read("apps/Module One/Signals/signal_workbench.fap"),
                    b"signal workbench",
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
                    archive.read("apps/Module One/BLE/app_bridge_terminal.fap"),
                    b"app bridge terminal",
                )
                self.assertEqual(
                    archive.read("apps/Module One/Macros/tumo_macro_deck.fap"),
                    b"macro deck",
                )
                self.assertEqual(
                    archive.read("apps/Module One/Scripts/tumoscript.fap"),
                    b"tumoscript",
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

    def test_package_only_release_retires_external_frequency_analyzers(self) -> None:
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
            self.assertNotIn("/ext/apps/ARF Tools/arf_frequency_analyzer.fap", arf_entries)
            self.assertNotIn(
                "/ext/apps/Module One/Sub-GHz/freq_analyzer_ext.fap",
                arf_entries,
            )
            self.assertIn(
                {
                    "group": "arf",
                    "legacy": "/ext/apps/Module One/Sub-GHz/freq_analyzer_ext.fap",
                    "canonical": "/ext/apps/ARF Tools/arf_subghz_full.fap",
                },
                manifest["cleanup"],
            )

    def test_package_only_release_retires_legacy_wifi_mapping_app(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            stale_visible = resources / "apps/Module One/ESP32 Wi-Fi/wifi_map.fap"
            write_file(stale_visible, b"legacy wifi mapping")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-tmwhflpprarf089-031",
                package_id="retire-legacy-wifi-map",
                target_release_tag="v0.3.1",
            )

            self.assertFalse(stale_visible.exists())
            module_entries = {
                entry["target"]: entry for entry in manifest["packages"]["module_one"]
            }
            self.assertIn(
                "/ext/apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
                module_entries,
            )
            self.assertNotIn(
                "/ext/apps/Module One/ESP32 Wi-Fi/wifi_map.fap",
                module_entries,
            )
            self.assertIn(
                {
                    "group": "module_one",
                    "legacy": "/ext/apps/Module One/ESP32 Wi-Fi/wifi_map.fap",
                    "canonical": "/ext/apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
                },
                manifest["cleanup"],
            )


if __name__ == "__main__":
    unittest.main()
