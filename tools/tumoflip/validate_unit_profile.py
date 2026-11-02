"""Validate the separate core test image; never installs or runs it on a device."""

import argparse
import csv
import json
from pathlib import Path
import re
import subprocess

try:
    from .validate_release import (
        FLASH_BASE, UPDATER_LIMIT, crc32, elf_flash_end, little_endian_hex, parse_fuf,
        resources_archive_hashes, sha256, validate_c2_safety, validate_dfuse_matches_elf,
    )
except ImportError:
    from validate_release import (
        FLASH_BASE, UPDATER_LIMIT, crc32, elf_flash_end, little_endian_hex, parse_fuf,
        resources_archive_hashes, sha256, validate_c2_safety, validate_dfuse_matches_elf,
    )

ROOT = Path(__file__).resolve().parents[2]
C2_ADDRESS = 0x080D7000
MINIMUM_C2_RESERVE = 4096


def check_host_imports(public, private, resident, plugins):
    missing_exports = private - resident
    if missing_exports:
        raise ValueError(f"Unit-host exports absent from ELF: {sorted(missing_exports)}")
    for name, imports in plugins.items():
        missing = imports - public - private
        if missing:
            raise ValueError(f"{name}: unresolved host imports {sorted(missing)}")
    return {name: len(imports) for name, imports in plugins.items()}


def declared_plugins(manifest):
    result = {name + ".fal" for name in re.findall(r'appid\s*=\s*"(test_\w+)"', manifest)}
    if not result or "test_js.fal" in result:
        raise ValueError("Core profile must declare tests without the excluded JS-host suite")
    return result


def check_plugin_set(expected, actual):
    if expected != actual:
        raise ValueError(f"Diagnostic plugin set mismatch: missing={sorted(expected-actual)}, "
                         f"extra={sorted(actual-expected)}")


def nm_symbols(nm, path, option):
    output = subprocess.check_output([str(nm), option, str(path)], text=True)
    return {line.split()[-1] for line in output.splitlines() if line.split()}


def artifact(directory, name):
    if not name or Path(name).name != name or "\\" in name:
        raise ValueError(f"Unsafe updater artifact name: {name!r}")
    path = directory / name
    if not path.is_file() or path.resolve().parent != directory.resolve():
        raise ValueError(f"Missing or escaped updater artifact: {name}")
    return path


def validate(root, build, expect_commit=None):
    identity = json.loads((build / "firmware.json").read_text())
    if identity["firmware_target"] != 7:
        raise ValueError("Unit profile is not an F7 image")
    if expect_commit and identity["firmware_commit"] != expect_commit[:8]:
        raise ValueError("Diagnostic build source does not match the expected commit")
    update = root / "dist/f7-C" / ("f7-update-" + identity["firmware_version"])
    fuf = parse_fuf(update / "update.fuf")
    if fuf["Target"] != "7" or fuf["Info"] != identity["firmware_version"]:
        raise ValueError("Updater target/version disagrees with the diagnostic build")
    files = {key: artifact(update, fuf[key])
             for key in ("Loader", "Firmware", "Radio", "Resources", "Splashscreen")}
    if files["Loader"].stat().st_size > UPDATER_LIMIT:
        raise ValueError("Diagnostic updater exceeds its size limit")
    for key in ("Loader", "Radio"):
        if crc32(files[key]) != little_endian_hex(fuf[key + " CRC"]):
            raise ValueError(f"{key} CRC disagrees with update.fuf")
    radio = little_endian_hex(fuf["Radio address"])
    if radio != C2_ADDRESS:
        raise ValueError("Unexpected radio boundary for the pinned F7 profile")
    end = elf_flash_end(root, build / "firmware.elf", radio)
    dfuse_end = validate_dfuse_matches_elf(files["Firmware"], radio, end)
    container_gap = radio - FLASH_BASE - files["Firmware"].stat().st_size
    aligned, gap = validate_c2_safety(radio, end, container_gap, MINIMUM_C2_RESERVE)

    nm = root / "toolchain/current/bin/arm-none-eabi-nm"
    resident = nm_symbols(nm, build / "firmware.elf", "--defined-only")
    # LTO removes the intermediate API pointer; the C++ hashtable object and
    # registered CLI entry remain. Do not require an optimized-away alias.
    if "unit_tests_on_system_start" not in resident or not any(
        "unit_tests_hashtable_api_interface" in name for name in resident
    ):
        raise ValueError("ELF does not contain the core diagnostic runner/resolver")
    if "js_thread_run" in resident or "js_app" in resident:
        raise ValueError("Excluded JS host unexpectedly became resident")
    manifest = (root / "applications/debug/unit_tests/application.fam").read_text()
    expected = declared_plugins(manifest)
    extapps = build / ".extapps"
    check_plugin_set(expected, {p.name for p in extapps.glob("test_*.fal")})
    table = (root / "applications/debug/unit_tests/unit_test_api_table_i.h").read_text()
    private = set(re.findall(r"API_(?:METHOD|VARIABLE)\(\s*(\w+)\s*,", table))
    with (root / "targets/f7/api_symbols.csv").open() as stream:
        public = {row[2] for row in csv.reader(stream)
                  if len(row) >= 3 and row[0] in ("Function", "Variable") and row[1] == "+"}
    imports = {name: nm_symbols(nm, extapps / name, "-u") for name in sorted(expected)}
    resolved = check_host_imports(public, private, resident, imports)
    hashes = resources_archive_hashes(root, files["Resources"])
    prefix = "apps_data/unit_tests/plugins/"
    actual = {name[len(prefix):] for name in hashes if name.startswith(prefix)}
    check_plugin_set(expected, actual)
    for name in expected:
        if hashes[prefix + name] != sha256(extapps / name):
            raise ValueError(f"Diagnostic archive contains stale {name}")
    required_ir = {
        "unit_tests/infrared/empty_database.ir":
            root / "applications/debug/unit_tests/resources/unit_tests/infrared/empty_database.ir",
        "unit_tests/infrared/unexpected_button.ir":
            root / "applications/debug/unit_tests/resources/unit_tests/infrared/unexpected_button.ir",
        "infrared/assets/projectors.ir":
            root / "applications/main/infrared/resources/infrared/assets/projectors.ir",
    }
    for name, path in required_ir.items():
        if hashes.get(name) != sha256(path):
            raise ValueError(f"Diagnostic IR fixture missing or stale: {name}")
    if any(name.startswith("unit_tests/js/") for name in hashes):
        raise ValueError("Dormant JS fixtures leaked into core test resources")
    return {
        "kind": "diagnosticProfileBuildNotDeviceExecution", "sourceIdentity": identity,
        "expectedCommitVerified": bool(expect_commit), "physicalC2Reserve": gap,
        "dfuseEnd": f"0x{dfuse_end:08X}", "eraseEnd": f"0x{aligned:08X}",
        "updaterBytes": files["Loader"].stat().st_size,
        "privateHostExports": len(private), "pluginImportCounts": resolved,
        "artifacts": {p.name: {"bytes": p.stat().st_size, "sha256": sha256(p)}
                      for p in files.values()},
        "javascriptCoverage": "excluded-source-only", "hardwareTestsRun": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=ROOT)
    parser.add_argument("--build-dir", type=Path, default=Path("build/f7-firmware-C"))
    parser.add_argument("--expect-commit")
    args = parser.parse_args()
    root = args.repo_root.resolve()
    try:
        report = validate(root, root / args.build_dir, args.expect_commit)
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"FAIL: {error}\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
