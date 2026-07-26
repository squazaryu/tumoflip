#!/usr/bin/env python3

import copy
import hashlib
import io
import os
import struct
import tarfile
import tempfile
import unittest
import zlib
from pathlib import Path

try:
    from .validate_release import (
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_DATA_FILES,
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        STATIC_SD_RESOURCES,
        ValidationError,
        _load_heatshrink2,
        crc32,
        find_objdump,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
        resources_archive_hashes,
        runtime_capabilities,
        validate_runtime_contract,
        validate_static_sd_resources,
    )
except ImportError:
    from validate_release import (
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_DATA_FILES,
        MODULE_ONE_PACKAGE_FILES,
        PROTOCOL_PACKS,
        STATIC_SD_RESOURCES,
        ValidationError,
        _load_heatshrink2,
        crc32,
        find_objdump,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
        resources_archive_hashes,
        runtime_capabilities,
        validate_runtime_contract,
        validate_static_sd_resources,
    )


REPO_ROOT = Path(__file__).resolve().parents[2]


class ValidateReleaseTest(unittest.TestCase):
    def test_protocol_pack_inventory_covers_active_arf_registry(self) -> None:
        self.assertEqual(len(PROTOCOL_PACKS), 30)
        self.assertIn("protocol_ford_v3.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_gm_rolling.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_kia_v7.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_star_line.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_toyota_lexus.fal", PROTOCOL_PACKS)

    def test_visible_arf_tools_inventory(self) -> None:
        self.assertEqual(
            ARF_VISIBLE_APP_IDS,
            {
                "arf_subghz_full",
                "garage_door_remote",
                "keeloq_keystore_decryptor",
                "subghz_raw_edit",
            },
        )

    def test_parse_fuf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "update.fuf"
            path.write_text(
                "Filetype: Test\n# comment\nVersion: 2\nRadio address: 00 70 0D 08\n",
                encoding="utf-8",
            )
            self.assertEqual(parse_fuf(path)["Version"], "2")
            self.assertEqual(parse_fuf(path)["Radio address"], "00 70 0D 08")

    def test_little_endian_hex(self) -> None:
        self.assertEqual(little_endian_hex("00 70 0D 08"), 0x080D7000)

    def test_crc32(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.bin"
            path.write_bytes(b"tumoflip")
            self.assertEqual(crc32(path), zlib.crc32(b"tumoflip") & 0xFFFFFFFF)

    def test_static_sd_resources_are_build_inputs(self) -> None:
        firmware = (REPO_ROOT / "firmware.scons").read_text(encoding="utf-8")
        resource_builder = (
            REPO_ROOT / "scripts/fbt_tools/fbt_resources.py"
        ).read_text(encoding="utf-8")

        self.assertIn("_STATIC_SD_RESOURCES", firmware)
        self.assertIn("#/tools/tumoflip/sd_resources", firmware)
        self.assertIn('env.get("_STATIC_SD_RESOURCES", [])', resource_builder)
        self.assertIn('env.GlobRecursive("*", static_root)', resource_builder)
        self.assertIn("Static resource target collision", resource_builder)

    def test_static_sd_resources_must_match_build_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory) / "repo"
            source = repo_root / STATIC_SD_RESOURCES / "apps/demo.fap"
            resources = Path(directory) / "resources"
            target = resources / "apps/demo.fap"
            source.parent.mkdir(parents=True)
            target.parent.mkdir(parents=True)
            source.write_bytes(b"same")
            target.write_bytes(b"same")

            validate_static_sd_resources(repo_root, resources)
            target.write_bytes(b"different")
            with self.assertRaises(ValidationError):
                validate_static_sd_resources(repo_root, resources)

    def test_resources_archive_hashes_decodes_ths(self) -> None:
        payload = b"routed-fap"
        plain_tar = io.BytesIO()
        with tarfile.open(fileobj=plain_tar, mode="w:") as archive:
            info = tarfile.TarInfo("apps/demo.fap")
            info.size = len(payload)
            archive.addfile(info, io.BytesIO(payload))

        heatshrink2 = _load_heatshrink2(REPO_ROOT)
        compressed = heatshrink2.compress(
            plain_tar.getvalue(), window_sz2=13, lookahead_sz2=6
        )
        with tempfile.TemporaryDirectory() as directory:
            archive_path = Path(directory) / "resources.ths"
            archive_path.write_bytes(
                struct.pack("<IBBB", 0x53445348, 1, 13, 6) + compressed
            )
            hashes = resources_archive_hashes(REPO_ROOT, archive_path)

        self.assertEqual(hashes["apps/demo.fap"], hashlib.sha256(payload).hexdigest())

    def test_find_objdump_falls_back_to_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "repo"
            root.mkdir()
            bin_dir = Path(directory) / "bin"
            bin_dir.mkdir()
            objdump = bin_dir / "arm-none-eabi-objdump"
            objdump.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            objdump.chmod(0o755)

            old_path = os.environ.get("PATH", "")
            try:
                os.environ["PATH"] = f"{bin_dir}{os.pathsep}{old_path}"
                self.assertEqual(find_objdump(root), objdump)
            finally:
                os.environ["PATH"] = old_path

    def test_manifest_release_id_is_stable_and_content_addressed(self) -> None:
        manifest = {"schema": 2, "packages": {"base": [{"sha256": "abc"}]}}
        reordered = {"packages": {"base": [{"sha256": "abc"}]}, "schema": 2}
        changed = copy.deepcopy(manifest)
        changed["packages"]["base"][0]["sha256"] = "def"

        self.assertEqual(manifest_release_id(manifest), manifest_release_id(reordered))
        self.assertNotEqual(manifest_release_id(manifest), manifest_release_id(changed))

    def test_static_module_one_package_files_are_vendored(self) -> None:
        self.assertIn(
            "apps/Module One/Diagnostics/cockpit.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertNotIn(
            "apps_data/module_one_cockpit/modules/tumo_uart_console.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Diagnostics/tumo_acceptance_suite.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Diagnostics/runtime_trace_viewer.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Field/field_logger.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Signals/signal_workbench.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertNotIn(
            "apps/Module One/Signals/protocol_compiler.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Signals/tumoscope.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Network/tumonet_gateway.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/VGM/tumovgm_bridge.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Modules/tumomodule_runtime.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Labs/tumokey_phase_a.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertNotIn(
            "apps/Module One/Security/tumokey.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Labs/tumofabric_node.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/NFC/tumotag_verify.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps_data/tumoflip_xremote/components/tumoflip_xremote_ac.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Sensors BME280/module_one_sensor_logger.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/BLE/ble_gatt_lab.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/BLE/app_bridge_terminal.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Automation/tumoflow.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Macros/tumo_macro_deck.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/Scripts/tumoscript.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/IR Blaster/tumo_ir_lab.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/ESP32 Wi-Fi/esp32_wifi_marauder.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertIn(
            "apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        marauder_manifest = (
            REPO_ROOT / "applications_user/esp32_wifi_marauder/application.fam"
        ).read_text(encoding="utf-8")
        self.assertIn('appid="esp32_wifi_marauder"', marauder_manifest)
        self.assertIn(
            'fap_category="Module One/ESP32 Wi-Fi"', marauder_manifest
        )
        self.assertFalse(
            (
                REPO_ROOT
                / STATIC_SD_RESOURCES
                / "apps/Module One/ESP32 Wi-Fi/esp32_wifi_marauder.fap"
            ).exists()
        )
        self.assertIn(
            "apps_data/tumo_macro_deck/macros/safe_demo.tmacro",
            MODULE_ONE_PACKAGE_DATA_FILES,
        )
        self.assertIn(
            "apps_data/tumoflow/workflows/field_demo.tflow",
            MODULE_ONE_PACKAGE_DATA_FILES,
        )
        self.assertIn(
            "apps_data/tumoflow/workflows/bounded_outputs.tflow",
            MODULE_ONE_PACKAGE_DATA_FILES,
        )
        self.assertIn(
            "apps_data/tumoscript/scripts/safe_demo.tscr",
            MODULE_ONE_PACKAGE_DATA_FILES,
        )
        self.assertIn(
            "apps_data/tumomodule_runtime/modules/bme280.tmod",
            MODULE_ONE_PACKAGE_DATA_FILES,
        )
        self.assertIn(
            "apps_data/tumomodule_runtime/modules/tumovgm.tmod",
            MODULE_ONE_PACKAGE_DATA_FILES,
        )
        self.assertIn(
            "apps_data/signal_workbench/profiles/demo_pulse_pair.tproto",
            MODULE_ONE_PACKAGE_DATA_FILES,
        )
        for path in (
            "apps_data/signal_workbench/demo/validation.sub",
            "subghz/TumoSpectrum Samples/train_0.sub",
            "subghz/TumoSpectrum Samples/train_1.sub",
            "subghz/TumoSpectrum Samples/train_2.sub",
            "subghz/TumoSpectrum Samples/train_3.sub",
        ):
            self.assertIn(path, MODULE_ONE_PACKAGE_DATA_FILES)
        sample = (
            REPO_ROOT
            / STATIC_SD_RESOURCES
            / "apps_data/tumo_macro_deck/macros/safe_demo.tmacro"
        )
        self.assertTrue(sample.is_file(), str(sample))
        tumoscript_sample = (
            REPO_ROOT
            / STATIC_SD_RESOURCES
            / "apps_data/tumoscript/scripts/safe_demo.tscr"
        )
        self.assertTrue(tumoscript_sample.is_file(), str(tumoscript_sample))
        for workflow_name in ("field_demo.tflow", "bounded_outputs.tflow"):
            workflow = (
                REPO_ROOT
                / STATIC_SD_RESOURCES
                / "apps_data/tumoflow/workflows"
                / workflow_name
            )
            self.assertTrue(workflow.is_file(), str(workflow))
        for module_manifest in ("bme280.tmod", "tumovgm.tmod"):
            sample = (
                REPO_ROOT
                / STATIC_SD_RESOURCES
                / "apps_data/tumomodule_runtime/modules"
                / module_manifest
            )
            self.assertTrue(sample.is_file(), str(sample))

    def test_runtime_contract_is_validated_for_release(self) -> None:
        capabilities = runtime_capabilities(REPO_ROOT)
        self.assertLessEqual(len(capabilities), 160)
        for required in (
            "runtime=1",
            "fab=2",
            "session=3",
            "status=2",
            "trace=1",
            "twin=1",
            "pkg=1",
            "radio=2",
            "sd=1",
            "feat=pkg,radio,trace,twin",
        ):
            self.assertIn(required, capabilities)

        validate_runtime_contract(REPO_ROOT)


if __name__ == "__main__":
    unittest.main()
