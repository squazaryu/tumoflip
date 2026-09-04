#!/usr/bin/env python3
"""Validate a tumoflip updater and emit its SD package manifest."""

from __future__ import annotations

import argparse
import hashlib
import importlib
import io
import json
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zlib
from pathlib import Path


FLASH_BASE = 0x08000000
UPDATER_LIMIT = 128 * 1024
# The updater erases the page containing each DfuSe element and the radio stack
# starts on an erase-page boundary. The C1 image may therefore occupy the last
# C1 page; validation must ensure that the aligned erase range does not cross
# the C2/radio boundary rather than requiring an unused page of headroom.
STM32WB55_FLASH_ERASE_PAGE_BYTES = 4096
DEFAULT_MIN_C2_GAP = 0
DFUSE_PREFIX = struct.Struct("<5sBIB")
DFUSE_TARGET_PREFIX = struct.Struct("<6sBB3s255sII")
DFUSE_ELEMENT_HEADER = struct.Struct("<II")
DFUSE_SUFFIX = struct.Struct("<HHHH3sBI")
DFUSE_DEVICE_CODE = 0xFFFF
DFUSE_PRODUCT_ID = 0xDF11
DFUSE_VENDOR_ID = 0x0483
DFUSE_SUFFIX_VERSION = 0x011A
DFUSE_WHOLE_FILE_CRC = 0xFFFFFFFF
PROTOCOL_PACKS = {
    "protocol_chrysler.fal",
    "protocol_fiat_marelli.fal",
    "protocol_ford_v0.fal",
    "protocol_ford_v1.fal",
    "protocol_ford_v2.fal",
    "protocol_ford_v3.fal",
    "protocol_gm_rolling.fal",
    "protocol_honda_acura.fal",
    "protocol_hyundai_new.fal",
    "protocol_kia_v0.fal",
    "protocol_kia_v1.fal",
    "protocol_kia_v2.fal",
    "protocol_kia_v3_v4.fal",
    "protocol_kia_v5.fal",
    "protocol_kia_v6.fal",
    "protocol_kia_v7.fal",
    "protocol_land_rover_v0.fal",
    "protocol_mazda_siemens.fal",
    "protocol_mazda_v0.fal",
    "protocol_mitsubishi_v0.fal",
    "protocol_nissan.fal",
    "protocol_porsche_cayenne.fal",
    "protocol_psa.fal",
    "protocol_renault.fal",
    "protocol_scher_khan.fal",
    "protocol_sheriff_cfm.fal",
    "protocol_star_line.fal",
    "protocol_subaru.fal",
    "protocol_superrollo.fal",
    "protocol_toyota_lexus.fal",
    "protocol_vag.fal",
}
ARF_VISIBLE_APP_IDS = {
    "arf_subghz_full",
    "garage_door_remote",
    "keeloq_keystore_decryptor",
    "subghz_raw_edit",
}
ARF_MODULE_APP_IDS = {
    "arf_car_emulate",
    "arf_counter_bf",
    "arf_keeloq",
    "arf_psa_decrypt",
    "arf_status",
    "proto_pirate",
    "rolljam",
    "subghz_bruteforcer",
}
ARF_APP_IDS = ARF_VISIBLE_APP_IDS | ARF_MODULE_APP_IDS
ARF_MODULE_ROOT = "/ext/apps_data/arf_subghz_full/modules"
ARF_MODULE_PATHS = {
    appid: f"{ARF_MODULE_ROOT}/{appid}.fap" for appid in ARF_MODULE_APP_IDS
}
ARF_VISIBLE_PATHS = {
    appid: f"/ext/apps/ARF Tools/{appid}.fap" for appid in ARF_VISIBLE_APP_IDS
}
ARF_EXTAPP_TARGETS = {
    **{f"{appid}.fap": path for appid, path in ARF_VISIBLE_PATHS.items()},
    **{
        f"{appid}.fap": path
        for appid, path in ARF_MODULE_PATHS.items()
    },
}
STATIC_SD_RESOURCES = Path("tools/tumoflip/sd_resources")
MODULE_ONE_PACKAGE_FILES = (
    "apps/Module One/Diagnostics/cockpit.fap",
    "apps/Module One/Diagnostics/tumo_acceptance_suite.fap",
    "apps/Module One/Diagnostics/runtime_trace_viewer.fap",
    "apps/Module One/Field/field_logger.fap",
    "apps/Module One/Signals/signal_workbench.fap",
    "apps/Module One/Signals/tumoscope.fap",
    "apps/Module One/Network/tumonet_gateway.fap",
    "apps/Module One/Sub-GHz/tumonet_bench.fap",
    "apps/Module One/VGM/tumovgm_bridge.fap",
    "apps/Module One/Modules/tumomodule_runtime.fap",
    "apps/Module One/Sensors BME280/module_one_sensor_logger.fap",
    "apps/Module One/BLE/ble_gatt_lab.fap",
    "apps/Module One/BLE/app_bridge_terminal.fap",
    "apps/Module One/Automation/tumoflow.fap",
    "apps/Module One/Macros/tumo_macro_deck.fap",
    "apps/Module One/Scripts/tumoscript.fap",
    "apps/Module One/IR Blaster/tumo_ir_lab.fap",
    "apps/Module One/IR Blaster/tumoflip_xremote.fap",
    "apps_data/tumoflip_xremote/components/tumoflip_xremote_ac.fap",
    "apps/Module One/ESP32 Wi-Fi/esp32_wifi_marauder.fap",
    "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap",
    "apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
    "apps/Module One/Labs/tumovm_poc.fap",
    "apps/Module One/Labs/tumovm_peripherals.fap",
    "apps/Module One/Labs/tumofabric_node.fap",
    "apps/Module One/Labs/tumokey_phase_a.fap",
    "apps/Module One/NFC/nfc_ccid_bridge.fap",
    "apps/Module One/NFC/tumocard_os.fap",
    "apps/Module One/NFC/tumotag_verify.fap",
)
# These files are built from the same source/API as the firmware, but are
# intentionally absent from resources.ths. They are delivered only through
# tumoflip-packages.zip because their lifecycle is owned by FW Packages.
QUAC_PACKAGE_FILE = "apps/Tools/quac.fap"
PACKAGE_ONLY_PACKAGE_FILES = frozenset(
    {
        "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap",
        QUAC_PACKAGE_FILE,
    }
)
TOTP_CLI_PLUGIN_APP_IDS = (
    "totp_cli_add_plugin",
    "totp_cli_automation_plugin",
    "totp_cli_delete_plugin",
    "totp_cli_details_plugin",
    "totp_cli_export_plugin",
    "totp_cli_help_plugin",
    "totp_cli_list_plugin",
    "totp_cli_move_plugin",
    "totp_cli_notification_plugin",
    "totp_cli_pin_plugin",
    "totp_cli_reset_plugin",
    "totp_cli_timezone_plugin",
    "totp_cli_update_plugin",
    "totp_cli_version_plugin",
)
TOTP_CLI_PLUGIN_PACKAGE_FILES = tuple(
    f"apps_data/totp/plugins/{appid}.fal" for appid in TOTP_CLI_PLUGIN_APP_IDS
)
MORSE_PLAYER_PACKAGE_FILE = "apps/Tools/morse_player.fap"

# Independent FW Packages revisions may replace files that are also bundled in
# updater resources. This is intentionally broader than PACKAGE_ONLY_PACKAGE_FILES:
# the latter remains reserved for artifacts that must never inflate resources.ths.
PACKAGE_RELEASE_OVERLAY_FILES = frozenset(
    {
        *PACKAGE_ONLY_PACKAGE_FILES,
        "apps/ARF Tools/subghz_raw_edit.fap",
        MORSE_PLAYER_PACKAGE_FILE,
        *TOTP_CLI_PLUGIN_PACKAGE_FILES,
    }
)
PACKAGE_ONLY_PACKAGE_GROUPS = {
    "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap": "module_one",
    QUAC_PACKAGE_FILE: "base",
}
PACKAGE_RELEASE_OVERLAY_GROUPS = {
    **PACKAGE_ONLY_PACKAGE_GROUPS,
    "apps/ARF Tools/subghz_raw_edit.fap": "arf",
    MORSE_PLAYER_PACKAGE_FILE: "base",
    **{relative: "base" for relative in TOTP_CLI_PLUGIN_PACKAGE_FILES},
}
MODULE_ONE_PACKAGE_DATA_FILES = (
    "apps_data/tumoflow/workflows/field_demo.tflow",
    "apps_data/tumoflow/workflows/bounded_outputs.tflow",
    "apps_data/tumo_macro_deck/macros/safe_demo.tmacro",
    "apps_data/tumoscript/scripts/safe_demo.tscr",
    "apps_data/tumomodule_runtime/modules/bme280.tmod",
    "apps_data/tumomodule_runtime/modules/tumovgm.tmod",
    "apps_data/tumovm_peripherals/packages/nfc_state.tper",
    "apps_data/tumovm_peripherals/packages/usb_media.tper",
    "apps_data/signal_workbench/profiles/demo_pulse_pair.tproto",
    "apps_data/signal_workbench/demo/validation.sub",
    "subghz/TumoSpectrum Samples/train_0.sub",
    "subghz/TumoSpectrum Samples/train_1.sub",
    "subghz/TumoSpectrum Samples/train_2.sub",
    "subghz/TumoSpectrum Samples/train_3.sub",
)
ARF_LEGACY_PATHS = {
    **{
        f"/ext/apps/ARF Tools/{appid}.fap": canonical
        for appid, canonical in ARF_MODULE_PATHS.items()
    },
    "/ext/apps/ARF Tools/ProtoPirate.fap": ARF_MODULE_PATHS["proto_pirate"],
    "/ext/apps/ARF Tools/ARF Sub-GHz Full.fap": "/ext/apps/ARF Tools/arf_subghz_full.fap",
    "/ext/apps/ARF Tools/ARF Status.fap": ARF_MODULE_PATHS["arf_status"],
    "/ext/apps/ARF Tools/Sub-GHz Bruteforcer.fap": ARF_MODULE_PATHS[
        "subghz_bruteforcer"
    ],
    "/ext/apps/ARF Tools/ARF KeeLoq.fap": ARF_MODULE_PATHS["arf_keeloq"],
    "/ext/apps/ARF Tools/ARF Counter BF.fap": ARF_MODULE_PATHS["arf_counter_bf"],
    "/ext/apps/ARF Tools/ARF Car Emulate.fap": ARF_MODULE_PATHS["arf_car_emulate"],
    "/ext/apps/ARF Tools/arf_frequency_analyzer.fap": ARF_VISIBLE_PATHS[
        "arf_subghz_full"
    ],
    "/ext/apps/ARF Tools/ARF Frequency Analyzer.fap": ARF_VISIBLE_PATHS[
        "arf_subghz_full"
    ],
    "/ext/apps/Module One/Sub-GHz/freq_analyzer_ext.fap": ARF_VISIBLE_PATHS[
        "arf_subghz_full"
    ],
    f"{ARF_MODULE_ROOT}/arf_frequency_analyzer.fap": ARF_VISIBLE_PATHS[
        "arf_subghz_full"
    ],
    f"{ARF_MODULE_ROOT}/arf_subghz_standard.fap": ARF_VISIBLE_PATHS["arf_subghz_full"],
    "/ext/apps/ARF Tools/arf_subghz_standard.fap": ARF_VISIBLE_PATHS["arf_subghz_full"],
    "/ext/apps/ARF Tools/ARF PSA Decrypt.fap": ARF_MODULE_PATHS["arf_psa_decrypt"],
    "/ext/apps/ARF Tools/ARF Sub-GHz.fap": "/ext/apps/ARF Tools/arf_subghz_full.fap",
    "/ext/apps/ARF Tools/arf_subghz.fap": "/ext/apps/ARF Tools/arf_subghz_full.fap",
    "/ext/apps/ARF Tools/rolljam_standalone.fap": ARF_MODULE_PATHS["rolljam"],
    "/ext/apps/ARF Tools/ble_killer.fap": ARF_VISIBLE_PATHS["arf_subghz_full"],
}
MODULE_ONE_LEGACY_PATHS = {
    "/ext/apps/GPIO/esp_flasher.fap":
        "/ext/apps/Module One/ESP32 Wi-Fi/esp_flasher.fap",
    "/ext/apps/Module One/ESP32 Wi-Fi/wifi_map.fap":
        "/ext/apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap",
    "/ext/apps/Module One/Diagnostics/module_one_cockpit.fap":
        "/ext/apps/Module One/Diagnostics/cockpit.fap",
    "/ext/apps/Module One/module_one_cockpit.fap":
        "/ext/apps/Module One/Diagnostics/cockpit.fap",
    "/ext/apps/Module One/tumo_acceptance_suite.fap":
        "/ext/apps/Module One/Diagnostics/tumo_acceptance_suite.fap",
    "/ext/apps/Module One/module_one_sensor_logger.fap":
        "/ext/apps/Module One/Sensors BME280/module_one_sensor_logger.fap",
    "/ext/apps/Module One/Signals/protocol_compiler.fap":
        "/ext/apps/Module One/Signals/signal_workbench.fap",
}
BASE_LEGACY_PATHS = {
    "/ext/apps/Scripts/js_app.fap": "/ext/apps/Bluetooth/flipper_companion.fap",
}
RELEASE_CLEANUP_PATHS = {
    **BASE_LEGACY_PATHS,
    **ARF_LEGACY_PATHS,
    **MODULE_ONE_LEGACY_PATHS,
}
RUNTIME_CAPABILITIES_MAX_BYTES = 160
RUNTIME_REQUIRED_CAPABILITIES = {
    "runtime=1",
    "fab=2",
    "session=3",
    "status=2",
    "trace=1",
    "twin=1",
    "pkg=1",
    "radio=2",
    "sd=1",
    "fabric=1",
}
RUNTIME_REQUIRED_FEATURES = {
    "pkg",
    "radio",
    "trace",
    "transfer",
    "twin",
    "fabric",
}
RUNTIME_REQUIRED_STATUS_FIELDS = (
    "schema=2",
    "fw=%.8s",
    "commit=%.8s",
    "dirty=%hhu",
    "origin=%.4s",
    "api=%hu.%hu",
    "target=%hhu",
    "transfer=%hhu",
    "sd=%hhu",
    "pkg=%hhu",
    "sid=%08lX",
    "bo=%.8s",
    "radio=%hhu",
    "owner=%.4s",
)
RUNTIME_REQUIRED_TRACE_FIELDS = (
    "schema=1;depth=%u;count=%u;drop=%lu",
    '"|%c,%c,%c"',
    "runtime->trace_dropped",
)
RUNTIME_REQUIRED_TWIN_FIELDS = (
    "schema=1;fw=%.8s;cm=%.8s;dy=%hhu;sd=%hhu;pkg=%hhu;bat=%u;chg=%hhu;otg=%hhu;",
    "heap=%lu;rf=%hhu;ro=%.4s;sid=%08lX;bo=%.8s",
    "furi_hal_power_is_charging()",
    "furi_hal_power_is_otg_enabled()",
    "memmgr_heap_get_max_free_block()",
)


class ValidationError(RuntimeError):
    pass


def resource_path_from_ext_target(path: str) -> str:
    if not path.startswith("/ext/"):
        raise ValidationError(f"Package target is outside /ext: {path}")
    return path.removeprefix("/ext/")


def release_cleanup_entries() -> list[dict[str, str]]:
    return [
        {"group": "base", "legacy": legacy, "canonical": canonical}
        for legacy, canonical in sorted(BASE_LEGACY_PATHS.items())
    ] + [
        {"group": "arf", "legacy": legacy, "canonical": canonical}
        for legacy, canonical in sorted(ARF_LEGACY_PATHS.items())
    ] + [
        {"group": "module_one", "legacy": legacy, "canonical": canonical}
        for legacy, canonical in sorted(MODULE_ONE_LEGACY_PATHS.items())
    ]


def package_extapp_exports() -> dict[str, str]:
    exports = {Path(relative).name: relative for relative in MODULE_ONE_PACKAGE_FILES}
    exports["morse_player.fap"] = MORSE_PLAYER_PACKAGE_FILE
    exports["quac.fap"] = QUAC_PACKAGE_FILE
    exports["module_one_cockpit.fap"] = "apps/Module One/Diagnostics/cockpit.fap"
    exports.update(
        {
            source_filename: resource_path_from_ext_target(target)
            for source_filename, target in ARF_EXTAPP_TARGETS.items()
        }
    )
    exports.update(
        {
            Path(relative).name: relative
            for relative in TOTP_CLI_PLUGIN_PACKAGE_FILES
        }
    )
    return exports


def parse_fuf(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, value = line.split(":", 1)
        values[key.strip()] = value.strip()
    return values


def little_endian_hex(value: str) -> int:
    try:
        return int.from_bytes(bytes.fromhex(value), "little")
    except ValueError as error:
        raise ValidationError(f"Invalid little-endian hex value: {value}") from error


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def md5(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def crc32(path: Path) -> int:
    checksum = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum = zlib.crc32(chunk, checksum)
    return checksum & 0xFFFFFFFF


def manifest_release_id(manifest: dict[str, object]) -> str:
    encoded = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def find_objdump(repo_root: Path) -> Path:
    candidates = (
        repo_root / "toolchain/current/bin/arm-none-eabi-objdump",
        repo_root / "toolchain/x86_64-linux/bin/arm-none-eabi-objdump",
        repo_root / "toolchain/arm64-linux/bin/arm-none-eabi-objdump",
        repo_root / "toolchain/arm64-darwin/bin/arm-none-eabi-objdump",
        repo_root / "toolchain/x86_64-darwin/bin/arm-none-eabi-objdump",
        repo_root / "toolchain/x86_64-windows/bin/arm-none-eabi-objdump.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    path_candidate = shutil.which("arm-none-eabi-objdump")
    if path_candidate:
        return Path(path_candidate)

    raise ValidationError(
        "arm-none-eabi-objdump was not found in the workspace toolchain or PATH"
    )


def elf_flash_end(repo_root: Path, elf_path: Path, radio_address: int) -> int:
    output = subprocess.run(
        [str(find_objdump(repo_root)), "-h", str(elf_path)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    section_pattern = re.compile(
        r"^\s*\d+\s+\S+\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)"
    )
    flash_end = FLASH_BASE
    for index, line in enumerate(output[:-1]):
        match = section_pattern.match(line)
        if not match or "LOAD" not in output[index + 1]:
            continue
        size = int(match.group(1), 16)
        load_address = int(match.group(2), 16)
        if FLASH_BASE <= load_address < radio_address:
            flash_end = max(flash_end, load_address + size)
    if flash_end == FLASH_BASE:
        raise ValidationError("No loadable firmware sections were found in the ELF")
    return flash_end


def dfuse_c1_flash_end(dfu_path: Path, radio_address: int) -> int:
    """Parse a DfuSe image and return the highest C1 byte written, exclusive.

    Release validation must derive the C1 layout from the addresses that the
    updater will actually flash. A DfuSe container may hold more than one
    element, so its total byte size is not a valid memory-layout proxy.
    """
    data = dfu_path.read_bytes()
    if len(data) < DFUSE_PREFIX.size + DFUSE_SUFFIX.size:
        raise ValidationError(f"DfuSe image is too small: {dfu_path}")

    signature, version, image_size, target_count = DFUSE_PREFIX.unpack_from(data)
    if signature != b"DfuSe" or version != 1:
        raise ValidationError(f"Invalid DfuSe prefix: {dfu_path}")
    if image_size != len(data) - DFUSE_SUFFIX.size:
        raise ValidationError(
            f"DfuSe image size does not match its suffix: {dfu_path}"
        )
    if target_count == 0:
        raise ValidationError(f"DfuSe image has no targets: {dfu_path}")

    (
        device_code,
        product_id,
        vendor_id,
        suffix_version,
        suffix_signature,
        suffix_length,
        _,
    ) = DFUSE_SUFFIX.unpack_from(data, image_size)
    if (
        device_code != DFUSE_DEVICE_CODE
        or product_id != DFUSE_PRODUCT_ID
        or vendor_id != DFUSE_VENDOR_ID
        or suffix_version != DFUSE_SUFFIX_VERSION
        or suffix_signature != b"UFD"
        or suffix_length != DFUSE_SUFFIX.size
    ):
        raise ValidationError(f"Invalid DfuSe suffix: {dfu_path}")
    if (zlib.crc32(data) & 0xFFFFFFFF) != DFUSE_WHOLE_FILE_CRC:
        raise ValidationError(f"DfuSe CRC is invalid: {dfu_path}")

    offset = DFUSE_PREFIX.size
    flash_end = FLASH_BASE
    for target_index in range(target_count):
        if offset + DFUSE_TARGET_PREFIX.size > image_size:
            raise ValidationError(
                f"DfuSe target {target_index} header is truncated: {dfu_path}"
            )
        (
            target_signature,
            _,
            _,
            _,
            _,
            target_size,
            element_count,
        ) = DFUSE_TARGET_PREFIX.unpack_from(data, offset)
        if target_signature != b"Target":
            raise ValidationError(f"Invalid DfuSe target {target_index}: {dfu_path}")
        if element_count == 0:
            raise ValidationError(
                f"DfuSe target {target_index} has no elements: {dfu_path}"
            )

        offset += DFUSE_TARGET_PREFIX.size
        target_end = offset + target_size
        if target_end > image_size:
            raise ValidationError(
                f"DfuSe target {target_index} exceeds image bounds: {dfu_path}"
            )

        for element_index in range(element_count):
            if offset + DFUSE_ELEMENT_HEADER.size > target_end:
                raise ValidationError(
                    "DfuSe element header is truncated: "
                    f"target={target_index}, element={element_index}, file={dfu_path}"
                )
            element_address, element_size = DFUSE_ELEMENT_HEADER.unpack_from(
                data, offset
            )
            offset += DFUSE_ELEMENT_HEADER.size
            if element_size == 0:
                raise ValidationError(
                    "DfuSe element is empty: "
                    f"target={target_index}, element={element_index}, file={dfu_path}"
                )
            element_end = element_address + element_size
            if element_end > 0x1_0000_0000 or offset + element_size > target_end:
                raise ValidationError(
                    "DfuSe element exceeds image bounds: "
                    f"target={target_index}, element={element_index}, file={dfu_path}"
                )
            if element_address % STM32WB55_FLASH_ERASE_PAGE_BYTES != 0:
                raise ValidationError(
                    "DfuSe element is not erase-page aligned: "
                    f"target={target_index}, element={element_index}, file={dfu_path}"
                )
            if element_address < FLASH_BASE:
                raise ValidationError(
                    "DfuSe element starts outside C1 flash: "
                    f"target={target_index}, element={element_index}, "
                    f"address=0x{element_address:08X}"
                )
            if element_address >= radio_address or element_end >= radio_address:
                raise ValidationError(
                    "DfuSe element writes C2/radio region: "
                    f"target={target_index}, element={element_index}, "
                    f"range=0x{element_address:08X}-0x{element_end:08X}, "
                    f"radio=0x{radio_address:08X}"
                )
            flash_end = max(flash_end, element_end)
            offset += element_size

        if offset != target_end:
            raise ValidationError(
                "DfuSe target size does not match its elements: "
                f"target={target_index}, file={dfu_path}"
            )

    if offset != image_size:
        raise ValidationError(f"DfuSe image has trailing data: {dfu_path}")
    if flash_end == FLASH_BASE:
        raise ValidationError(f"DfuSe image has no C1 elements: {dfu_path}")
    return flash_end


def validate_dfuse_matches_elf(
    dfu_path: Path, radio_address: int, elf_end: int
) -> int:
    dfuse_end = dfuse_c1_flash_end(dfu_path, radio_address)
    if dfuse_end != elf_end:
        raise ValidationError(
            "DfuSe C1 flash end does not match ELF: "
            f"DfuSe=0x{dfuse_end:08X}, ELF=0x{elf_end:08X}"
        )
    return dfuse_end


def align_up(value: int, alignment: int) -> int:
    if alignment <= 0:
        raise ValueError("alignment must be positive")
    return (value + alignment - 1) // alignment * alignment


def validate_c2_safety(
    radio_address: int,
    flash_end: int,
    dfu_container_gap: int,
    min_c2_gap: int,
) -> tuple[int, int]:
    """Require the updater's aligned C1 erase range to stay below C2.

    The DfuSe file contains metadata that is not flashed to C1. Its byte size
    is retained as a release diagnostic; its flashed elements must match ELF.
    """
    required_gap = max(min_c2_gap, 0)
    erase_aligned_flash_end = align_up(
        flash_end, STM32WB55_FLASH_ERASE_PAGE_BYTES
    )
    physical_gap = radio_address - erase_aligned_flash_end
    section_gap = radio_address - flash_end
    if physical_gap < required_gap:
        raise ValidationError(
            "C1 erase range reaches the C2/radio region: "
            f"physical={physical_gap}, sections={section_gap}, "
            f"DFU container={dfu_container_gap}, required={required_gap} bytes"
        )
    return erase_aligned_flash_end, physical_gap


def api_version(api_symbols: Path) -> str:
    for line in api_symbols.read_text(encoding="utf-8").splitlines():
        if line.startswith("Version,+,"):
            return line.split(",", 3)[2]
    raise ValidationError("Firmware API version is missing from api_symbols.csv")


def require_file(path: Path, label: str) -> Path:
    if not path.is_file():
        raise ValidationError(f"Missing {label}: {path}")
    return path


def runtime_capabilities(repo_root: Path) -> str:
    runtime = require_file(
        repo_root / "applications/services/tumoflip_runtime/tumoflip_runtime.c",
        "Tumoflip Runtime source",
    ).read_text(encoding="utf-8")
    lines = runtime.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("#define TUMOFLIP_RUNTIME_CAPABILITIES"):
            continue

        parts = []
        for value_line in lines[index + 1 :]:
            strings = re.findall(r'"([^"]*)"', value_line)
            if not strings:
                break
            parts.extend(strings)
            if not value_line.rstrip().endswith("\\"):
                break
        if not parts:
            raise ValidationError("Tumoflip Runtime capabilities payload is empty")
        return "".join(parts)

    raise ValidationError("Tumoflip Runtime capabilities macro is missing")


def validate_runtime_contract(repo_root: Path) -> None:
    capabilities = runtime_capabilities(repo_root)
    capability_bytes = len(capabilities.encode("ascii"))
    if capability_bytes > RUNTIME_CAPABILITIES_MAX_BYTES:
        raise ValidationError(
            "Tumoflip Runtime capabilities payload is too large: "
            f"{capability_bytes} > {RUNTIME_CAPABILITIES_MAX_BYTES} bytes"
        )

    capability_fields = set(capabilities.split(";"))
    missing = sorted(RUNTIME_REQUIRED_CAPABILITIES - capability_fields)
    if missing:
        raise ValidationError(f"Tumoflip Runtime capabilities missing: {missing}")

    feature_field = next((field for field in capability_fields if field.startswith("feat=")), "")
    features = set(feature_field.removeprefix("feat=").split(","))
    missing_features = sorted(RUNTIME_REQUIRED_FEATURES - features)
    if missing_features:
        raise ValidationError(
            f"Tumoflip Runtime capabilities features missing: {missing_features}"
        )

    runtime = (
        repo_root / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
    ).read_text(encoding="utf-8")
    for required in RUNTIME_REQUIRED_STATUS_FIELDS:
        if required not in runtime:
            raise ValidationError(f"Tumoflip Runtime status field is missing: {required}")
    for required in RUNTIME_REQUIRED_TRACE_FIELDS:
        if required not in runtime:
            raise ValidationError(f"Tumoflip Runtime trace field is missing: {required}")
    for required in RUNTIME_REQUIRED_TWIN_FIELDS:
        if required not in runtime:
            raise ValidationError(f"Tumoflip Runtime twin field is missing: {required}")
    if "storage_sd_status(runtime->storage)" not in runtime:
        raise ValidationError("Tumoflip Runtime status must expose SD readiness")


def install_static_sd_resources(repo_root: Path, resources: Path) -> None:
    source_root = repo_root / STATIC_SD_RESOURCES
    if not source_root.is_dir():
        return

    for source in source_root.rglob("*"):
        if not source.is_file():
            continue
        relative = source.relative_to(source_root)
        target = resources / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())


def validate_static_sd_resources(repo_root: Path, resources: Path) -> None:
    source_root = repo_root / STATIC_SD_RESOURCES
    if not source_root.is_dir():
        raise ValidationError(f"Static SD resource root is missing: {source_root}")

    for source in source_root.rglob("*"):
        if not source.is_file():
            continue
        relative = source.relative_to(source_root)
        target = require_file(resources / relative, f"static SD resource {relative}")
        if sha256(source) != sha256(target):
            raise ValidationError(f"Static SD resource differs from build output: {relative}")


def sync_extapp_package_exports(
    build_dir: Path,
    resources: Path,
    only_targets: frozenset[str] | None = None,
) -> list[dict[str, object]]:
    extapps = build_dir / ".extapps"
    if not extapps.is_dir():
        prune_legacy_resource_exports(resources)
        return []

    synced: list[dict[str, object]] = []
    for filename, relative in sorted(package_extapp_exports().items()):
        if only_targets is not None and relative not in only_targets:
            continue
        source = extapps / filename
        if not source.is_file():
            continue
        target = resources / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        synced.append(
            {
                "source": source.relative_to(build_dir).as_posix(),
                "target": relative,
                "bytes": target.stat().st_size,
                "sha256": sha256(target),
                "md5": md5(target),
            }
        )
    prune_legacy_resource_exports(resources)
    return synced


def validate_extapp_package_exports(
    repo_root: Path, build_dir: Path, resources: Path
) -> None:
    require_file(build_dir / "firmware.json", "firmware metadata")
    extapps = build_dir / ".extapps"
    if not extapps.is_dir():
        raise ValidationError(f"External app build directory is missing: {extapps}")

    sources_by_target: dict[str, list[Path]] = {}
    for filename, relative in package_extapp_exports().items():
        sources_by_target.setdefault(relative, []).append(extapps / filename)

    for relative, candidates in sorted(sources_by_target.items()):
        target = require_file(resources / relative, f"packaged external app {relative}")
        sources = [candidate for candidate in candidates if candidate.is_file()]
        if not sources:
            static_source = repo_root / STATIC_SD_RESOURCES / relative
            if static_source.is_file():
                continue
            candidate_names = ", ".join(str(candidate) for candidate in candidates)
            raise ValidationError(
                f"External app build artifact is missing for {relative}: {candidate_names}"
            )
        target_hash = sha256(target)
        if all(sha256(source) != target_hash for source in sources):
            raise ValidationError(f"External app differs from build artifacts: {relative}")


def _load_heatshrink2(repo_root: Path):
    try:
        return importlib.import_module("heatshrink2")
    except ImportError:
        pass

    site_packages = sorted(
        (repo_root / "toolchain/current/lib").glob("python*/site-packages")
    )
    for candidate in site_packages:
        sys.path.insert(0, str(candidate))
        try:
            return importlib.import_module("heatshrink2")
        except ImportError:
            continue

    raise ValidationError("heatshrink2 was not found in Python or the workspace toolchain")


def resources_archive_hashes(repo_root: Path, archive_path: Path) -> dict[str, str]:
    data = require_file(archive_path, "resources archive").read_bytes()
    if len(data) < 7:
        raise ValidationError("resources.ths is too small")

    magic, version, window, lookahead = struct.unpack("<IBBB", data[:7])
    if magic != 0x53445348 or version != 1:
        raise ValidationError("resources.ths has an invalid heatshrink header")

    heatshrink2 = _load_heatshrink2(repo_root)
    try:
        plain_tar = heatshrink2.decompress(
            data[7:], window_sz2=window, lookahead_sz2=lookahead
        )
        with tarfile.open(fileobj=io.BytesIO(plain_tar), mode="r:") as archive:
            hashes: dict[str, str] = {}
            for member in archive.getmembers():
                if not member.isfile():
                    continue
                stream = archive.extractfile(member)
                if stream is None:
                    raise ValidationError(f"resources.ths entry cannot be read: {member.name}")
                hashes[member.name] = hashlib.sha256(stream.read()).hexdigest()
            return hashes
    except Exception as error:
        if isinstance(error, ValidationError):
            raise
        raise ValidationError(f"resources.ths cannot be decoded: {error}") from error


def validate_resources_archive(
    repo_root: Path,
    archive_path: Path,
    packages: dict[str, list[dict[str, object]]],
) -> None:
    archive_hashes = resources_archive_hashes(repo_root, archive_path)
    accidentally_bundled = sorted(PACKAGE_ONLY_PACKAGE_FILES.intersection(archive_hashes))
    if accidentally_bundled:
        raise ValidationError(
            "Package-only files must not be bundled in resources.ths: "
            f"{accidentally_bundled}"
        )
    for entries in packages.values():
        for entry in entries:
            relative = str(entry["target"]).removeprefix("/ext/")
            if relative in PACKAGE_ONLY_PACKAGE_FILES:
                continue
            archive_hash = archive_hashes.get(relative)
            if archive_hash is None:
                raise ValidationError(f"Package entry is missing from resources.ths: {relative}")
            if archive_hash != entry["sha256"]:
                raise ValidationError(f"Package entry differs in resources.ths: {relative}")


def prune_legacy_resource_exports(resources: Path) -> None:
    for legacy, canonical in RELEASE_CLEANUP_PATHS.items():
        if legacy == canonical:
            continue
        legacy_relative = resource_path_from_ext_target(legacy)
        canonical_relative = resource_path_from_ext_target(canonical)
        if legacy_relative == canonical_relative:
            continue
        legacy_path = resources / legacy_relative
        if legacy_path.is_file():
            legacy_path.unlink()


def package_entries(resources: Path) -> dict[str, list[dict[str, object]]]:
    groups: dict[str, list[Path]] = {
        "base": [
            resources / "apps/Bluetooth/claude_buddy.fap",
            resources / "apps/Bluetooth/flipper_companion.fap",
            resources / "apps/Sub-GHz/subghz_wardriving.fap",
            resources / "apps/Tools/ai_dashboard.fap",
            resources / "apps/Tools/clock.fap",
            resources / "apps/Tools/flipper_relay.fap",
            resources / MORSE_PLAYER_PACKAGE_FILE,
            resources / "apps/Tools/quac.fap",
            resources / "apps/Tools/tumoflip_packages.fap",
            resources / "apps/Tools/totp.fap",
        ]
        + [resources / relative for relative in TOTP_CLI_PLUGIN_PACKAGE_FILES],
        "module_one": [
            resources / relative
            for relative in (*MODULE_ONE_PACKAGE_FILES, *MODULE_ONE_PACKAGE_DATA_FILES)
        ],
        "arf": sorted((resources / "apps/ARF Tools").glob("*.fap"))
        + sorted((resources / "apps_data/arf_subghz_full/modules").glob("*.fap")),
        "protocol_packs": sorted(
            (resources / "apps_data/subghz/plugins").glob("protocol_*.fal")
        ),
    }
    proto_assets = resources / "apps_assets/proto_pirate"
    if proto_assets.is_dir():
        groups["arf"].extend(
            sorted(path for path in proto_assets.rglob("*") if path.is_file())
        )

    result: dict[str, list[dict[str, object]]] = {}
    for group, paths in groups.items():
        if not paths:
            raise ValidationError(f"Package group is empty: {group}")
        entries = []
        for path in paths:
            require_file(path, f"{group} package entry")
            relative = path.relative_to(resources).as_posix()
            entries.append(
                {
                    "source": relative,
                    "target": f"/ext/{relative}",
                    "bytes": path.stat().st_size,
                    "sha256": sha256(path),
                    "md5": md5(path),
                }
            )
        result[group] = entries
    return result


def validate_layout(resources: Path) -> None:
    protocol_dir = resources / "apps_data/subghz/plugins"
    actual_protocols = {path.name for path in protocol_dir.glob("protocol_*.fal")}
    if actual_protocols != PROTOCOL_PACKS:
        missing = sorted(PROTOCOL_PACKS - actual_protocols)
        extra = sorted(actual_protocols - PROTOCOL_PACKS)
        raise ValidationError(
            f"Protocol Pack set mismatch; missing={missing}, extra={extra}"
        )

    misplaced = []
    for path in (resources / "apps/Sub-GHz").glob("*.fap"):
        if path.stem in ARF_APP_IDS:
            misplaced.append(path.name)
    if misplaced:
        raise ValidationError(
            f"Duplicate ARF apps remain in apps/Sub-GHz: {sorted(misplaced)}"
        )

    visible_arf = {path.stem for path in (resources / "apps/ARF Tools").glob("*.fap")}
    if visible_arf != ARF_VISIBLE_APP_IDS:
        raise ValidationError(
            "ARF Tools visible set mismatch; "
            f"missing={sorted(ARF_VISIBLE_APP_IDS - visible_arf)}, "
            f"extra={sorted(visible_arf - ARF_VISIBLE_APP_IDS)}"
        )

    module_dir = resources / "apps_data/arf_subghz_full/modules"
    actual_modules = {path.stem for path in module_dir.glob("*.fap")}
    if actual_modules != ARF_MODULE_APP_IDS:
        raise ValidationError(
            "ARF Full module set mismatch; "
            f"missing={sorted(ARF_MODULE_APP_IDS - actual_modules)}, "
            f"extra={sorted(actual_modules - ARF_MODULE_APP_IDS)}"
        )


def validate_release(
    repo_root: Path,
    update_dir: Path,
    build_dir: Path,
    min_c2_gap: int,
    write_manifest: bool,
) -> dict[str, object]:
    validate_runtime_contract(repo_root)

    fuf_path = require_file(update_dir / "update.fuf", "update manifest")
    fuf = parse_fuf(fuf_path)
    if fuf.get("Target") != "7":
        raise ValidationError(f"Unexpected hardware target: {fuf.get('Target')}")

    referenced = {
        key: require_file(update_dir / fuf[key], key)
        for key in ("Loader", "Firmware", "Radio", "Resources", "Splashscreen")
        if fuf.get(key)
    }
    loader = referenced["Loader"]
    firmware = referenced["Firmware"]
    radio = referenced["Radio"]
    if loader.stat().st_size > UPDATER_LIMIT:
        raise ValidationError(
            f"Updater is too large: {loader.stat().st_size} > {UPDATER_LIMIT} bytes"
        )
    if crc32(loader) != little_endian_hex(fuf["Loader CRC"]):
        raise ValidationError("Updater CRC does not match update.fuf")
    if crc32(radio) != little_endian_hex(fuf["Radio CRC"]):
        raise ValidationError("Radio CRC does not match update.fuf")

    radio_address = little_endian_hex(fuf["Radio address"])
    # firmware.dfu includes DfuSe container metadata that is not flashed to C1.
    # Keep this coarse value in the manifest for diagnostics only. Its flashed
    # address range is parsed and cross-checked against the ELF below.
    dfu_container_gap = radio_address - FLASH_BASE - firmware.stat().st_size
    elf_path = require_file(build_dir / "firmware.elf", "firmware ELF")
    flash_end = elf_flash_end(repo_root, elf_path, radio_address)
    dfuse_flash_end = validate_dfuse_matches_elf(firmware, radio_address, flash_end)
    section_gap = radio_address - flash_end
    erase_aligned_flash_end, physical_c2_gap = validate_c2_safety(
        radio_address, flash_end, dfu_container_gap, min_c2_gap
    )

    firmware_json = json.loads(
        require_file(build_dir / "firmware.json", "firmware metadata").read_text(
            encoding="utf-8"
        )
    )
    if firmware_json["firmware_target"] != 7:
        raise ValidationError("firmware.json target is not Flipper Zero (7)")
    if firmware_json["firmware_version"] != fuf.get("Info"):
        raise ValidationError("Firmware version and update.fuf Info do not match")

    resources = require_file(
        build_dir / "resources/Manifest", "resource manifest"
    ).parent
    validate_static_sd_resources(repo_root, resources)
    validate_layout(resources)
    unexpectedly_bundled = sorted(
        relative
        for relative in PACKAGE_ONLY_PACKAGE_FILES
        if (resources / relative).exists()
    )
    if unexpectedly_bundled:
        raise ValidationError(
            "Package-only files are present in the updater resource tree: "
            f"{unexpectedly_bundled}"
        )

    # Build the standalone FW Packages view from a disposable staging tree.
    # The updater's resources and resources.ths remain immutable and exclude
    # large optional package-only FAPs.
    with tempfile.TemporaryDirectory(prefix="tumoflip-package-resources-") as directory:
        package_resources = Path(directory) / "resources"
        shutil.copytree(resources, package_resources)
        sync_extapp_package_exports(
            build_dir,
            package_resources,
            only_targets=PACKAGE_ONLY_PACKAGE_FILES,
        )
        validate_extapp_package_exports(repo_root, build_dir, package_resources)
        validate_layout(package_resources)
        packages = package_entries(package_resources)
        validate_resources_archive(repo_root, referenced["Resources"], packages)
        api = api_version(repo_root / "targets/f7/api_symbols.csv")
        manifest: dict[str, object] = {
            "schema": 2,
            "firmware": {
                "name": "tumoflip",
                "version": firmware_json["firmware_version"],
                "target": 7,
                "api": api,
                "radio_address": f"0x{radio_address:08X}",
            },
            "safety": {
                "minimum_c2_gap_bytes": max(min_c2_gap, 0),
                "dfu_gap_bytes": dfu_container_gap,
                "dfuse_flash_end": f"0x{dfuse_flash_end:08X}",
                "section_gap_bytes": section_gap,
                "erase_aligned_flash_end": f"0x{erase_aligned_flash_end:08X}",
                "physical_c2_gap_bytes": physical_c2_gap,
                "updater_bytes": loader.stat().st_size,
                "updater_limit_bytes": UPDATER_LIMIT,
            },
            "artifacts": {
                path.name: {"bytes": path.stat().st_size, "sha256": sha256(path)}
                for path in sorted(referenced.values())
            },
            "packages": packages,
            "cleanup": release_cleanup_entries(),
        }
        manifest["release_id"] = manifest_release_id(manifest)
        if write_manifest:
            output = update_dir / "tumoflip-packages.json"
            output.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            # Publish the companion install archive from the staged package
            # resources, not from resources.ths.
            try:
                from .make_packages_zip import build_packages_zip
            except ImportError:
                from make_packages_zip import build_packages_zip
            build_packages_zip(
                manifest,
                package_resources,
                update_dir / "tumoflip-packages.zip",
            )
        return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root", type=Path, default=Path(__file__).resolve().parents[2]
    )
    parser.add_argument("--build-dir", type=Path, default=Path("build/f7-firmware-C"))
    parser.add_argument("--update-dir", type=Path)
    parser.add_argument("--min-c2-gap", type=int, default=DEFAULT_MIN_C2_GAP)
    parser.add_argument("--write-manifest", action="store_true")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    build_dir = (repo_root / args.build_dir).resolve()
    if args.update_dir:
        update_dir = (repo_root / args.update_dir).resolve()
    else:
        firmware = json.loads((build_dir / "firmware.json").read_text(encoding="utf-8"))
        update_dir = (
            repo_root / "dist/f7-C" / f"f7-update-{firmware['firmware_version']}"
        )
    try:
        manifest = validate_release(
            repo_root, update_dir, build_dir, args.min_c2_gap, args.write_manifest
        )
    except (
        OSError,
        KeyError,
        ValueError,
        subprocess.CalledProcessError,
        ValidationError,
    ) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1

    safety = manifest["safety"]
    print(
        "OK: updater validated; "
        f"C2 physical gap={safety['physical_c2_gap_bytes']} bytes; "
        f"DfuSe C1 end={safety['dfuse_flash_end']}; "
        f"section gap={safety['section_gap_bytes']} bytes; "
        f"DFU container gap={safety['dfu_gap_bytes']} bytes; "
        f"updater={safety['updater_bytes']} bytes; "
        f"API={manifest['firmware']['api']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
