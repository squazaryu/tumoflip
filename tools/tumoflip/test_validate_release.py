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
        MODULE_ONE_LEGACY_PATHS,
        PACKAGE_ONLY_PACKAGE_FILES,
        PACKAGE_ONLY_PACKAGE_GROUPS,
        PROTOCOL_PACKS,
        STATIC_SD_RESOURCES,
        DEFAULT_MIN_C2_GAP,
        STM32WB55_FLASH_ERASE_PAGE_BYTES,
        FLASH_BASE,
        ValidationError,
        _load_heatshrink2,
        crc32,
        dfuse_c1_flash_end,
        find_objdump,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
        resources_archive_hashes,
        runtime_capabilities,
        validate_runtime_contract,
        validate_resources_archive,
        validate_c2_safety,
        validate_dfuse_matches_elf,
        validate_static_sd_resources,
    )
except ImportError:
    from validate_release import (
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_DATA_FILES,
        MODULE_ONE_PACKAGE_FILES,
        MODULE_ONE_LEGACY_PATHS,
        PACKAGE_ONLY_PACKAGE_FILES,
        PACKAGE_ONLY_PACKAGE_GROUPS,
        PROTOCOL_PACKS,
        STATIC_SD_RESOURCES,
        DEFAULT_MIN_C2_GAP,
        STM32WB55_FLASH_ERASE_PAGE_BYTES,
        FLASH_BASE,
        ValidationError,
        _load_heatshrink2,
        crc32,
        dfuse_c1_flash_end,
        find_objdump,
        little_endian_hex,
        manifest_release_id,
        parse_fuf,
        resources_archive_hashes,
        runtime_capabilities,
        validate_runtime_contract,
        validate_resources_archive,
        validate_c2_safety,
        validate_dfuse_matches_elf,
        validate_static_sd_resources,
    )


REPO_ROOT = Path(__file__).resolve().parents[2]


def make_dfuse(elements: list[tuple[int, bytes]]) -> bytes:
    element_data = b"".join(
        struct.pack("<II", address, len(payload)) + payload
        for address, payload in elements
    )
    target = struct.pack(
        "<6sBB3s255sII",
        b"Target",
        0,
        1,
        b"\0" * 3,
        b"C1".ljust(255, b"\0"),
        len(element_data),
        len(elements),
    ) + element_data
    image = struct.pack(
        "<5sBIB", b"DfuSe", 1, struct.calcsize("<5sBIB") + len(target), 1
    ) + target
    suffix = struct.pack(
        "<HHHH3sB", 0xFFFF, 0xDF11, 0x0483, 0x011A, b"UFD", 16
    )
    crc = ~zlib.crc32(image + suffix) & 0xFFFFFFFF
    return image + suffix + struct.pack("<I", crc)


class ValidateReleaseTest(unittest.TestCase):
    def test_protocol_pack_inventory_covers_active_arf_registry(self) -> None:
        self.assertEqual(len(PROTOCOL_PACKS), 31)
        self.assertIn("protocol_ford_v3.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_gm_rolling.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_kia_v7.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_star_line.fal", PROTOCOL_PACKS)
        self.assertIn("protocol_superrollo.fal", PROTOCOL_PACKS)
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

    def test_c2_safety_uses_physical_elf_layout_not_dfuse_metadata(self) -> None:
        radio_address = 0x080D7000
        # The final C1 page may be populated up to the C2 page boundary. The
        # updater erases 0x080D6000..0x080D6FFF, never the C2 page itself.
        flash_end = radio_address - 0xF8C
        dfu_container_gap = 4023

        erase_aligned_end, physical_gap = validate_c2_safety(
            radio_address,
            flash_end,
            dfu_container_gap,
            DEFAULT_MIN_C2_GAP,
        )

        self.assertLess(dfu_container_gap, STM32WB55_FLASH_ERASE_PAGE_BYTES)
        self.assertEqual(erase_aligned_end, radio_address)
        self.assertEqual(physical_gap, 0)

    def test_c2_safety_rejects_layout_that_crosses_the_radio_boundary(self) -> None:
        radio_address = 0x080D7000
        flash_end = radio_address + 1

        with self.assertRaisesRegex(
            ValidationError,
            r"C1 erase range reaches the C2/radio region: physical=-4096",
        ):
            validate_c2_safety(
                radio_address,
                flash_end,
                0,
                DEFAULT_MIN_C2_GAP,
            )

    def test_dfuse_c1_flash_end_matches_an_ordinary_elf_layout(self) -> None:
        radio_address = 0x080D7000
        payload = b"\xAA" * 16
        elf_end = FLASH_BASE + len(payload)

        with tempfile.TemporaryDirectory() as directory:
            dfu_path = Path(directory) / "firmware.dfu"
            dfu_path.write_bytes(make_dfuse([(FLASH_BASE, payload)]))

            self.assertEqual(dfuse_c1_flash_end(dfu_path, radio_address), elf_end)
            self.assertEqual(
                validate_dfuse_matches_elf(dfu_path, radio_address, elf_end),
                elf_end,
            )

    def test_dfuse_rejects_c2_element_even_with_an_ordinary_c1_elf(self) -> None:
        radio_address = 0x080D7000
        c1_payload = b"\xAA" * 16
        elf_end = FLASH_BASE + len(c1_payload)

        with tempfile.TemporaryDirectory() as directory:
            dfu_path = Path(directory) / "firmware.dfu"
            dfu_path.write_bytes(
                make_dfuse(
                    [
                        (FLASH_BASE, c1_payload),
                        (radio_address, b"\xBB" * 16),
                    ]
                )
            )

            with self.assertRaisesRegex(ValidationError, "writes C2/radio region"):
                validate_dfuse_matches_elf(dfu_path, radio_address, elf_end)

    def test_dfuse_flash_end_must_match_elf(self) -> None:
        radio_address = 0x080D7000
        payload = b"\xAA" * 16

        with tempfile.TemporaryDirectory() as directory:
            dfu_path = Path(directory) / "firmware.dfu"
            dfu_path.write_bytes(make_dfuse([(FLASH_BASE, payload)]))

            with self.assertRaisesRegex(ValidationError, "does not match ELF"):
                validate_dfuse_matches_elf(
                    dfu_path,
                    radio_address,
                    FLASH_BASE + len(payload) + 1,
                )

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

    def test_package_only_fap_is_excluded_from_updater_archive(self) -> None:
        package_only = "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap"
        self.assertEqual(PACKAGE_ONLY_PACKAGE_FILES, {package_only})
        self.assertEqual(
            PACKAGE_ONLY_PACKAGE_GROUPS,
            {package_only: "module_one"},
        )
        regular = "apps/Tools/clock.fap"
        regular_payload = b"clock"
        flasher_payload = b"large optional flasher"
        packages = {
            "base": [
                {
                    "target": f"/ext/{regular}",
                    "sha256": hashlib.sha256(regular_payload).hexdigest(),
                }
            ],
            "module_one": [
                {
                    "target": f"/ext/{package_only}",
                    "sha256": hashlib.sha256(flasher_payload).hexdigest(),
                }
            ],
        }

        def write_archive(path: Path, files: dict[str, bytes]) -> None:
            plain_tar = io.BytesIO()
            with tarfile.open(fileobj=plain_tar, mode="w:") as archive:
                for name, payload in files.items():
                    info = tarfile.TarInfo(name)
                    info.size = len(payload)
                    archive.addfile(info, io.BytesIO(payload))
            heatshrink2 = _load_heatshrink2(REPO_ROOT)
            compressed = heatshrink2.compress(
                plain_tar.getvalue(), window_sz2=13, lookahead_sz2=6
            )
            path.write_bytes(
                struct.pack("<IBBB", 0x53445348, 1, 13, 6) + compressed
            )

        with tempfile.TemporaryDirectory() as directory:
            archive_path = Path(directory) / "resources.ths"
            write_archive(archive_path, {regular: regular_payload})
            validate_resources_archive(REPO_ROOT, archive_path, packages)

            write_archive(
                archive_path,
                {regular: regular_payload, package_only: flasher_payload},
            )
            with self.assertRaisesRegex(
                ValidationError,
                "Package-only files must not be bundled",
            ):
                validate_resources_archive(REPO_ROOT, archive_path, packages)

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
            "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap",
            MODULE_ONE_PACKAGE_FILES,
        )
        self.assertEqual(
            MODULE_ONE_LEGACY_PATHS["/ext/apps/GPIO/esp_flasher.fap"],
            "/ext/apps/Module One/ESP32 Wi-Fi/esp_flasher.fap",
        )
        flasher_manifest = (
            REPO_ROOT / "applications_user/esp_flasher/application.fam"
        ).read_text(encoding="utf-8")
        self.assertIn('appid="esp_flasher"', flasher_manifest)
        self.assertIn('fap_category="Module One/ESP32 Wi-Fi"', flasher_manifest)
        self.assertIn("fap_package_only=True", flasher_manifest)
        appmanifest = (
            REPO_ROOT / "scripts/fbt/appmanifest.py"
        ).read_text(encoding="utf-8")
        self.assertIn("fap_package_only: bool = False", appmanifest)
        self.assertIn("and not self.fap_package_only", appmanifest)
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
