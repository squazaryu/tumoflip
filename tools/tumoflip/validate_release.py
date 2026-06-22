#!/usr/bin/env python3
"""Validate a tumoflip updater and emit its SD package manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import zlib
from pathlib import Path


FLASH_BASE = 0x08000000
UPDATER_LIMIT = 128 * 1024
# Keep at least one complete STM32WB55 flash erase page between the C1 image
# and the C2/radio region. This prevents a C1 erase operation from touching C2.
DEFAULT_MIN_C2_GAP = 4096
PROTOCOL_PACKS = {
    "protocol_chrysler.fal",
    "protocol_fiat_marelli.fal",
    "protocol_ford_v0.fal",
    "protocol_ford_v1.fal",
    "protocol_ford_v2.fal",
    "protocol_ford_v3.fal",
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
    "protocol_porsche_cayenne.fal",
    "protocol_psa.fal",
    "protocol_scher_khan.fal",
    "protocol_sheriff_cfm.fal",
    "protocol_star_line.fal",
    "protocol_subaru.fal",
    "protocol_vag.fal",
}
ARF_APP_IDS = {
    "arf_car_emulate",
    "arf_counter_bf",
    "arf_frequency_analyzer",
    "arf_keeloq",
    "arf_psa_decrypt",
    "arf_status",
    "arf_subghz_full",
    "proto_pirate",
    "rolljam",
    "subghz_bruteforcer",
}
ARF_LEGACY_PATHS = {
    "/ext/apps/ARF Tools/ProtoPirate.fap": "/ext/apps/ARF Tools/proto_pirate.fap",
    "/ext/apps/ARF Tools/ARF Sub-GHz Full.fap": "/ext/apps/ARF Tools/arf_subghz_full.fap",
    "/ext/apps/ARF Tools/ARF Status.fap": "/ext/apps/ARF Tools/arf_status.fap",
    "/ext/apps/ARF Tools/Sub-GHz Bruteforcer.fap": "/ext/apps/ARF Tools/subghz_bruteforcer.fap",
    "/ext/apps/ARF Tools/ARF KeeLoq.fap": "/ext/apps/ARF Tools/arf_keeloq.fap",
    "/ext/apps/ARF Tools/ARF Counter BF.fap": "/ext/apps/ARF Tools/arf_counter_bf.fap",
    "/ext/apps/ARF Tools/ARF Car Emulate.fap": "/ext/apps/ARF Tools/arf_car_emulate.fap",
    "/ext/apps/ARF Tools/ARF Frequency Analyzer.fap": "/ext/apps/ARF Tools/arf_frequency_analyzer.fap",
    "/ext/apps/ARF Tools/ARF PSA Decrypt.fap": "/ext/apps/ARF Tools/arf_psa_decrypt.fap",
}


class ValidationError(RuntimeError):
    pass


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
        repo_root / "toolchain/arm64-darwin/bin/arm-none-eabi-objdump",
        repo_root / "toolchain/x86_64-darwin/bin/arm-none-eabi-objdump",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise ValidationError(
        "arm-none-eabi-objdump was not found in the workspace toolchain"
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


def api_version(api_symbols: Path) -> str:
    for line in api_symbols.read_text(encoding="utf-8").splitlines():
        if line.startswith("Version,+,"):
            return line.split(",", 3)[2]
    raise ValidationError("Firmware API version is missing from api_symbols.csv")


def require_file(path: Path, label: str) -> Path:
    if not path.is_file():
        raise ValidationError(f"Missing {label}: {path}")
    return path


def package_entries(resources: Path) -> dict[str, list[dict[str, object]]]:
    groups: dict[str, list[Path]] = {
        "base": [
            resources / "apps/Bluetooth/flipper_companion.fap",
            resources / "apps/Tools/ai_dashboard.fap",
            resources / "apps/Tools/flipper_relay.fap",
            resources / "apps/Tools/quac.fap",
            resources / "apps/Tools/totp.fap",
        ],
        "module_one": [resources / "apps/Module One/IR Blaster/tumoflip_xremote.fap"],
        "arf": sorted((resources / "apps/ARF Tools").glob("*.fap")),
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

    arf_dir = resources / "apps/ARF Tools"
    actual_arf = {path.stem for path in arf_dir.glob("*.fap")}
    missing_arf = sorted(ARF_APP_IDS - actual_arf)
    if missing_arf:
        raise ValidationError(f"ARF Tools package is incomplete: {missing_arf}")


def validate_release(
    repo_root: Path,
    update_dir: Path,
    build_dir: Path,
    min_c2_gap: int,
    write_manifest: bool,
) -> dict[str, object]:
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
    coarse_gap = radio_address - FLASH_BASE - firmware.stat().st_size
    elf_path = require_file(build_dir / "firmware.elf", "firmware ELF")
    flash_end = elf_flash_end(repo_root, elf_path, radio_address)
    section_gap = radio_address - flash_end
    if coarse_gap < min_c2_gap or section_gap < min_c2_gap:
        raise ValidationError(
            f"C2 safety gap is too small: DFU={coarse_gap}, sections={section_gap}, "
            f"required={min_c2_gap} bytes"
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
    validate_layout(resources)
    packages = package_entries(resources)
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
            "minimum_c2_gap_bytes": min_c2_gap,
            "dfu_gap_bytes": coarse_gap,
            "section_gap_bytes": section_gap,
            "updater_bytes": loader.stat().st_size,
            "updater_limit_bytes": UPDATER_LIMIT,
        },
        "artifacts": {
            path.name: {"bytes": path.stat().st_size, "sha256": sha256(path)}
            for path in sorted(referenced.values())
        },
        "packages": packages,
        "cleanup": [
            {"legacy": legacy, "canonical": canonical}
            for legacy, canonical in sorted(ARF_LEGACY_PATHS.items())
        ],
    }
    manifest["release_id"] = manifest_release_id(manifest)
    if write_manifest:
        output = update_dir / "tumoflip-packages.json"
        output.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        # Also publish the companion install archive (issue #8) from the SAME manifest
        # and resources tree. Hardened + atomic; see make_packages_zip.py.
        try:
            from .make_packages_zip import build_packages_zip
        except ImportError:
            from make_packages_zip import build_packages_zip
        build_packages_zip(manifest, resources, update_dir / "tumoflip-packages.zip")
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
        f"C2 section gap={safety['section_gap_bytes']} bytes; "
        f"updater={safety['updater_bytes']} bytes; "
        f"API={manifest['firmware']['api']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
