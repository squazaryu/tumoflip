#!/usr/bin/env python3

import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path

try:
    from .apply_packages import load_manifest
    from .make_packages_zip import build_packages_zip
    from .package_release import (
        build_catalog_reconciliation,
        build_package_release,
        build_selective_catalog_release,
        catalog_lineage_base,
        selective_catalog_build_targets,
        selective_catalog_overlay_names,
        selective_catalog_overlay_sources,
    )
    from .validate_release import (
        ARF_MODULE_APP_IDS,
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_DATA_FILES,
        MODULE_ONE_PACKAGE_FILES,
        PACKAGE_RELEASE_OVERLAY_FILES,
        PROTOCOL_PACKS,
        TOTP_CLI_PLUGIN_PACKAGE_FILES,
        ValidationError,
        manifest_release_id,
        md5,
        package_entries,
        sha256,
    )
except ImportError:
    from apply_packages import load_manifest
    from make_packages_zip import build_packages_zip
    from package_release import (
        build_catalog_reconciliation,
        build_package_release,
        build_selective_catalog_release,
        catalog_lineage_base,
        selective_catalog_build_targets,
        selective_catalog_overlay_names,
        selective_catalog_overlay_sources,
    )
    from validate_release import (
        ARF_MODULE_APP_IDS,
        ARF_VISIBLE_APP_IDS,
        MODULE_ONE_PACKAGE_DATA_FILES,
        MODULE_ONE_PACKAGE_FILES,
        PACKAGE_RELEASE_OVERLAY_FILES,
        PROTOCOL_PACKS,
        TOTP_CLI_PLUGIN_PACKAGE_FILES,
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
    (repo / "tools/tumoflip").mkdir(parents=True)
    (repo / "tools/tumoflip/package_catalog_baselines.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "stable": {
                    "release_tag": "v1.0.4",
                    "firmware_version": "t-flppr-fw-004",
                    "api": "88.0",
                    "target": 7,
                },
                "dev": {
                    "release_tag": "t-dev-004-013",
                    "firmware_version": "t-dev-004-013",
                    "api": "88.0",
                    "target": 7,
                },
            }
        ),
        encoding="utf-8",
    )
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

    for relative in TOTP_CLI_PLUGIN_PACKAGE_FILES:
        write_file(resources / relative, relative.encode())

    # Package-only overlays are sourced from exact build outputs. Keep fixture
    # bytes identical to the resource tree unless a test explicitly replaces
    # one of them.
    for relative in PACKAGE_RELEASE_OVERLAY_FILES:
        source = resources / relative
        if source.is_file():
            write_file(build / ".extapps" / Path(relative).name, source.read_bytes())

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


def prepare_reconciliation_catalogs(
    root: Path,
) -> tuple[Path, Path, dict[str, object], Path, dict[str, object], Path, str]:
    repo, _build, resources = prepare_package_tree(root)
    baselines = json.loads(
        (repo / "tools/tumoflip/package_catalog_baselines.json").read_text()
    )
    baselines["dev"]["release_tag"] = "t-dev-004-015"
    baselines["dev"]["firmware_version"] = "t-dev-004-015"
    (repo / "tools/tumoflip/package_catalog_baselines.json").write_text(
        json.dumps(baselines), encoding="utf-8"
    )
    subprocess.run(
        ["git", "add", "tools/tumoflip/package_catalog_baselines.json"],
        cwd=repo,
        check=True,
    )
    subprocess.run(
        ["git", "commit", "-m", "accept dev baseline"],
        cwd=repo,
        check=True,
        capture_output=True,
    )
    source_commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=repo, text=True
    ).strip()

    outside = root / "catalog-assets"
    outside.mkdir()
    target_manifest, generated_target_zip = prepare_target_package(repo, resources)
    target_manifest["firmware"]["version"] = "t-dev-004-015"
    target_manifest.pop("release_id")
    target_manifest["release_id"] = manifest_release_id(target_manifest)
    target_zip = outside / "target.zip"
    shutil.move(generated_target_zip, target_zip)
    shutil.rmtree(repo / "target-release")

    compatible_resources = outside / "compatible-resources"
    shutil.copytree(resources, compatible_resources)
    overlay_sources = sorted(PACKAGE_RELEASE_OVERLAY_FILES)
    for index, source in enumerate(overlay_sources):
        write_file(
            compatible_resources / source,
            f"accepted overlay {index}: {source}".encode(),
        )
    # This old-baseline difference was never an explicit overlay and must not be
    # grandfathered by reconciliation.
    write_file(
        compatible_resources / "apps/Tools/quac.fap",
        b"old non-overlay quac",
    )
    compatible_manifest = {
        "schema": 2,
        "firmware": {
            **target_manifest["firmware"],
            "version": "t-dev-004-013",
        },
        "safety": target_manifest["safety"],
        "artifacts": target_manifest["artifacts"],
        "packages": package_entries(compatible_resources),
        "cleanup": target_manifest["cleanup"],
        "package_release": {
            "type": "package-only",
            "id": "fw-packages-dev-004",
            "source_commit": source_commit,
            "source_dirty": False,
            "source_firmware_version": "t-dev-004-015",
            "target_release_tag": "t-dev-004-013",
            "target_release_id": "a" * 64,
            "firmware_flash_unchanged": True,
            "overlay_targets": overlay_sources,
            "synced_extapps": [],
            "catalog_channel": "dev",
            "catalog_revision": 4,
            "catalog_release_tag": "fw-packages-dev-004",
        },
    }
    compatible_manifest["release_id"] = manifest_release_id(compatible_manifest)
    compatible_manifest_path = outside / "compatible.json"
    compatible_manifest_path.write_text(
        json.dumps(compatible_manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    compatible_zip = outside / "compatible.zip"
    build_packages_zip(compatible_manifest, compatible_resources, compatible_zip)
    self_status = subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=repo, text=True
    ).strip()
    if self_status:
        raise AssertionError(f"fixture repo is dirty: {self_status}")
    return (
        repo,
        outside,
        target_manifest,
        target_zip,
        compatible_manifest,
        compatible_zip,
        source_commit,
    )


def prepare_selective_catalogs(
    root: Path,
) -> tuple[
    Path,
    Path,
    Path,
    dict[str, object],
    Path,
    dict[str, object],
    Path,
    str,
    str,
]:
    (
        repo,
        outside,
        target_manifest,
        target_zip,
        compatible_manifest,
        compatible_zip,
        target_source_commit,
    ) = prepare_reconciliation_catalogs(root)
    base_dir = outside / "base-output"
    base_manifest = build_catalog_reconciliation(
        repo,
        base_dir,
        target_release_tag="t-dev-004-015",
        target_source_commit=target_source_commit,
        target_manifest=target_manifest,
        target_package_zip=target_zip,
        compatible_manifest=compatible_manifest,
        compatible_manifest_sha256=sha256(outside / "compatible.json"),
        compatible_package_zip=compatible_zip,
        compatible_release_tag="fw-packages-dev-004",
        catalog_release_tag="fw-packages-dev-005",
    )
    build = repo / "build/f7-firmware-C"
    base_manifest_path = base_dir / "tumoflip-packages.json"
    base_zip = base_dir / "tumoflip-packages.zip"
    base_release = base_manifest["package_release"]
    lineage = {
        "schema": 1,
        "dev": {
            "firmware_version": base_manifest["firmware"]["version"],
            "manifest_sha256": sha256(base_manifest_path),
            "package_zip_sha256": sha256(base_zip),
            "release_id": base_manifest["release_id"],
            "release_tag": base_release["catalog_release_tag"],
            "source_commit": base_release["source_commit"],
            "target_release_id": base_release["target_release_id"],
            "target_release_tag": base_release["target_release_tag"],
            "target_source_commit": target_source_commit,
        },
    }
    (repo / "tools/tumoflip/package_catalog_lineage.json").write_text(
        json.dumps(lineage, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_file(build / ".extapps/esp_flasher.fap", b"selective esp flasher update")
    subprocess.run(
        [
            "git",
            "add",
            "build/f7-firmware-C/.extapps/esp_flasher.fap",
            "tools/tumoflip/package_catalog_lineage.json",
        ],
        cwd=repo,
        check=True,
    )
    subprocess.run(
        ["git", "commit", "-m", "fix: update esp flasher"],
        cwd=repo,
        check=True,
        capture_output=True,
    )
    base_source_commit = str(base_manifest["package_release"]["source_commit"])
    return (
        repo,
        build,
        outside,
        target_manifest,
        target_zip,
        base_manifest,
        base_zip,
        target_source_commit,
        base_source_commit,
    )


class PackageReleaseTest(unittest.TestCase):
    def test_selective_catalog_allowlist_resolves_only_esp_flasher(self) -> None:
        source = "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap"
        self.assertEqual(
            selective_catalog_overlay_names("esp_flasher"),
            ("esp_flasher",),
        )
        self.assertEqual(
            selective_catalog_overlay_sources("esp_flasher"),
            frozenset({source}),
        )
        self.assertEqual(
            selective_catalog_build_targets("esp_flasher"),
            ("fap_esp_flasher",),
        )
        for unsafe in (
            "subghz_raw_edit",
            "../esp_flasher",
            "esp_flasher,",
            "esp_flasher,esp_flasher",
            "esp_flasher\nforged=1",
            "esp_flasher\r",
            "esp_flasher\x00",
            " esp_flasher",
        ):
            with self.subTest(unsafe=unsafe), self.assertRaises(ValidationError):
                selective_catalog_overlay_sources(unsafe)

    def test_selective_catalog_changes_only_esp_and_preserves_alias_lineage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                repo,
                build,
                outside,
                target_manifest,
                target_zip,
                base_manifest,
                base_zip,
                target_source_commit,
                base_source_commit,
            ) = prepare_selective_catalogs(root)
            base_manifest_path = outside / "base-output/tumoflip-packages.json"
            esp_source = "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap"

            manifest = build_selective_catalog_release(
                repo,
                build,
                outside / "selective-output",
                target_release_tag="t-dev-004-015",
                target_source_commit=target_source_commit,
                target_manifest=target_manifest,
                target_package_zip=target_zip,
                base_manifest=base_manifest,
                base_manifest_sha256=sha256(base_manifest_path),
                base_package_zip=base_zip,
                base_release_tag="fw-packages-dev-005",
                base_source_commit=base_source_commit,
                catalog_release_tag="fw-packages-dev-007",
                overlay_sources=frozenset({esp_source}),
            )

            def indexed(value: dict[str, object]) -> dict[str, dict[str, object]]:
                return {
                    entry["source"]: entry
                    for entries in value["packages"].values()
                    for entry in entries
                }

            before = indexed(base_manifest)
            after = indexed(manifest)
            self.assertEqual(set(before), set(after))
            base_payload = json.loads(json.dumps(base_manifest))
            output_payload = json.loads(json.dumps(manifest))
            for payload in (base_payload, output_payload):
                payload.pop("release_id")
                payload.pop("package_release")
            base_esp = indexed(base_payload)[esp_source]
            output_esp = indexed(output_payload)[esp_source]
            output_esp.update(base_esp)
            self.assertEqual(output_payload, base_payload)
            self.assertEqual(
                {source: entry for source, entry in after.items() if source != esp_source},
                {source: entry for source, entry in before.items() if source != esp_source},
            )
            self.assertNotEqual(after[esp_source]["sha256"], before[esp_source]["sha256"])
            self.assertNotIn("compatible_builds", after[esp_source])
            self.assertEqual(
                sum("compatible_builds" in entry for entry in before.values()),
                16,
            )
            self.assertEqual(
                sum("compatible_builds" in entry for entry in after.values()),
                15,
            )
            installed_dev004 = {
                source: entry["compatible_builds"][0]["md5"]
                for source, entry in before.items()
                if "compatible_builds" in entry
            }
            needs_update = {
                source
                for source, installed_md5 in installed_dev004.items()
                if installed_md5 != after[source]["md5"]
                and installed_md5
                not in {
                    alias["md5"]
                    for alias in after[source].get("compatible_builds", [])
                }
            }
            self.assertEqual(needs_update, {esp_source})

            release = manifest["package_release"]
            self.assertEqual(release["catalog_revision"], 7)
            self.assertEqual(release["overlay_targets"], [esp_source])
            self.assertEqual(release["catalog_modified_targets"], [esp_source])
            self.assertEqual(
                release["compatible_releases"],
                base_manifest["package_release"]["compatible_releases"],
            )
            self.assertEqual(
                release["base_catalog"],
                {
                    "release_tag": "fw-packages-dev-005",
                    "release_id": base_manifest["release_id"],
                    "manifest_sha256": sha256(base_manifest_path),
                    "package_zip_sha256": sha256(base_zip),
                    "source_commit": base_source_commit,
                },
            )
            with zipfile.ZipFile(base_zip) as old_zip, zipfile.ZipFile(
                outside / "selective-output/tumoflip-packages.zip"
            ) as new_zip:
                self.assertEqual(old_zip.namelist(), new_zip.namelist())
                for source in before:
                    if source != esp_source:
                        self.assertEqual(new_zip.read(source), old_zip.read(source))
                self.assertEqual(
                    new_zip.read(esp_source),
                    b"selective esp flasher update",
                )

    def test_selective_catalog_rejects_bad_lineage_and_noop(self) -> None:
        esp_source = "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                repo,
                build,
                outside,
                target_manifest,
                target_zip,
                base_manifest,
                base_zip,
                target_source_commit,
                base_source_commit,
            ) = prepare_selective_catalogs(root)
            base_manifest_path = outside / "base-output/tumoflip-packages.json"
            broken = json.loads(json.dumps(base_manifest))
            broken["package_release"]["target_release_id"] = "f" * 64
            broken.pop("release_id")
            broken["release_id"] = manifest_release_id(broken)
            with self.assertRaisesRegex(ValidationError, "pinned manifest_sha256 differs"):
                build_selective_catalog_release(
                    repo,
                    build,
                    outside / "broken-output",
                    target_release_tag="t-dev-004-015",
                    target_source_commit=target_source_commit,
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                    base_manifest=broken,
                    base_manifest_sha256="f" * 64,
                    base_package_zip=base_zip,
                    base_release_tag="fw-packages-dev-005",
                    base_source_commit=base_source_commit,
                    catalog_release_tag="fw-packages-dev-007",
                    overlay_sources=frozenset({esp_source}),
                )

            with zipfile.ZipFile(base_zip) as archive:
                write_file(build / ".extapps/esp_flasher.fap", archive.read(esp_source))
            subprocess.run(
                ["git", "add", "build/f7-firmware-C/.extapps/esp_flasher.fap"],
                cwd=repo,
                check=True,
            )
            subprocess.run(
                ["git", "commit", "-m", "test: restore base esp artifact"],
                cwd=repo,
                check=True,
                capture_output=True,
            )
            with self.assertRaisesRegex(ValidationError, "target is unchanged"):
                build_selective_catalog_release(
                    repo,
                    build,
                    outside / "noop-output",
                    target_release_tag="t-dev-004-015",
                    target_source_commit=target_source_commit,
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                    base_manifest=base_manifest,
                    base_manifest_sha256=sha256(base_manifest_path),
                    base_package_zip=base_zip,
                    base_release_tag="fw-packages-dev-005",
                    base_source_commit=base_source_commit,
                    catalog_release_tag="fw-packages-dev-007",
                    overlay_sources=frozenset({esp_source}),
                )

    def test_selective_catalog_rejects_unreferenced_compatible_release(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                repo,
                build,
                outside,
                target_manifest,
                target_zip,
                base_manifest,
                base_zip,
                target_source_commit,
                base_source_commit,
            ) = prepare_selective_catalogs(root)
            broken = json.loads(json.dumps(base_manifest))
            for entries in broken["packages"].values():
                for entry in entries:
                    entry.pop("compatible_builds", None)
            broken.pop("release_id")
            broken["release_id"] = manifest_release_id(broken)
            with self.assertRaisesRegex(
                ValidationError,
                "references differ from compatible releases",
            ):
                build_selective_catalog_release(
                    repo,
                    build,
                    outside / "orphan-output",
                    target_release_tag="t-dev-004-015",
                    target_source_commit=target_source_commit,
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                    base_manifest=broken,
                    base_manifest_sha256="f" * 64,
                    base_package_zip=base_zip,
                    base_release_tag="fw-packages-dev-005",
                    base_source_commit=base_source_commit,
                    catalog_release_tag="fw-packages-dev-007",
                    overlay_sources=frozenset(
                        {"apps/Module One/ESP32 Wi-Fi/esp_flasher.fap"}
                    ),
                )

    def test_selective_catalog_prunes_compatible_release_when_last_alias_is_selected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                repo,
                build,
                outside,
                target_manifest,
                target_zip,
                base_manifest,
                base_zip,
                target_source_commit,
                base_source_commit,
            ) = prepare_selective_catalogs(root)
            esp_source = "apps/Module One/ESP32 Wi-Fi/esp_flasher.fap"
            single_alias = json.loads(json.dumps(base_manifest))
            for entries in single_alias["packages"].values():
                for entry in entries:
                    if entry["source"] != esp_source:
                        entry.pop("compatible_builds", None)
            single_alias.pop("release_id")
            single_alias["release_id"] = manifest_release_id(single_alias)
            single_manifest_path = outside / "single-alias.json"
            single_manifest_path.write_text(
                json.dumps(single_alias, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            single_zip = outside / "single-alias.zip"
            with zipfile.ZipFile(base_zip) as archive:
                resources = outside / "single-resources"
                archive.extractall(resources)
            build_packages_zip(single_alias, resources, single_zip)
            lineage_path = repo / "tools/tumoflip/package_catalog_lineage.json"
            lineage = json.loads(lineage_path.read_text())
            lineage["dev"]["manifest_sha256"] = sha256(single_manifest_path)
            lineage["dev"]["package_zip_sha256"] = sha256(single_zip)
            lineage["dev"]["release_id"] = single_alias["release_id"]
            lineage_path.write_text(json.dumps(lineage), encoding="utf-8")
            subprocess.run(["git", "add", str(lineage_path)], cwd=repo, check=True)
            subprocess.run(
                ["git", "commit", "-m", "test: pin single alias base"],
                cwd=repo,
                check=True,
                capture_output=True,
            )

            manifest = build_selective_catalog_release(
                repo,
                build,
                outside / "pruned-output",
                target_release_tag="t-dev-004-015",
                target_source_commit=target_source_commit,
                target_manifest=target_manifest,
                target_package_zip=target_zip,
                base_manifest=single_alias,
                base_manifest_sha256=sha256(single_manifest_path),
                base_package_zip=single_zip,
                base_release_tag="fw-packages-dev-005",
                base_source_commit=base_source_commit,
                catalog_release_tag="fw-packages-dev-007",
                overlay_sources=frozenset({esp_source}),
            )
            self.assertEqual(manifest["package_release"]["compatible_releases"], [])

    def test_reconciled_catalog_accepts_only_exact_previous_overlays(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                repo,
                outside,
                target_manifest,
                target_zip,
                compatible_manifest,
                compatible_zip,
                source_commit,
            ) = prepare_reconciliation_catalogs(root)

            manifest = build_catalog_reconciliation(
                repo,
                outside / "output",
                target_release_tag="t-dev-004-015",
                target_source_commit=source_commit,
                target_manifest=target_manifest,
                target_package_zip=target_zip,
                compatible_manifest=compatible_manifest,
                compatible_manifest_sha256=sha256(outside / "compatible.json"),
                compatible_package_zip=compatible_zip,
                compatible_release_tag="fw-packages-dev-004",
                catalog_release_tag="fw-packages-dev-005",
            )

            entries = {
                entry["source"]: entry
                for group in manifest["packages"].values()
                for entry in group
            }
            overlays = compatible_manifest["package_release"]["overlay_targets"]
            self.assertEqual(
                {
                    source
                    for source, entry in entries.items()
                    if "compatible_builds" in entry
                },
                set(overlays),
            )
            self.assertNotIn("compatible_builds", entries["apps/Tools/quac.fap"])
            for source in overlays:
                alias = entries[source]["compatible_builds"]
                self.assertEqual(len(alias), 1)
                self.assertEqual(alias[0]["release_id"], compatible_manifest["release_id"])
            release = manifest["package_release"]
            self.assertEqual(release["overlay_targets"], [])
            self.assertEqual(release["target_release_tag"], "t-dev-004-015")
            self.assertEqual(release["catalog_revision"], 5)
            self.assertEqual(
                release["compatible_releases"][0]["manifest_sha256"],
                sha256(outside / "compatible.json"),
            )
            unsigned = dict(manifest)
            unsigned.pop("release_id")
            self.assertEqual(manifest["release_id"], manifest_release_id(unsigned))
            self.assertEqual(
                (outside / "output/tumoflip-packages.zip").read_bytes(),
                target_zip.read_bytes(),
            )

    def test_reconciled_catalog_rejects_different_overlay_source_commit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                repo,
                outside,
                target_manifest,
                target_zip,
                compatible_manifest,
                compatible_zip,
                source_commit,
            ) = prepare_reconciliation_catalogs(root)

            with self.assertRaisesRegex(
                ValidationError,
                "not built from the accepted firmware source commit",
            ):
                build_catalog_reconciliation(
                    repo,
                    outside / "output",
                    target_release_tag="t-dev-004-015",
                    target_source_commit="f" * 40,
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                    compatible_manifest=compatible_manifest,
                    compatible_manifest_sha256=sha256(outside / "compatible.json"),
                    compatible_package_zip=compatible_zip,
                    compatible_release_tag="fw-packages-dev-004",
                    catalog_release_tag="fw-packages-dev-005",
                )

    def test_reconciled_catalog_rejects_corrupt_compatible_zip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (
                repo,
                outside,
                target_manifest,
                target_zip,
                compatible_manifest,
                compatible_zip,
                source_commit,
            ) = prepare_reconciliation_catalogs(root)
            with zipfile.ZipFile(compatible_zip, "a") as archive:
                archive.writestr("unexpected.fap", b"bad")

            with self.assertRaisesRegex(ValidationError, "contents differ"):
                build_catalog_reconciliation(
                    repo,
                    outside / "output",
                    target_release_tag="t-dev-004-015",
                    target_source_commit=source_commit,
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                    compatible_manifest=compatible_manifest,
                    compatible_manifest_sha256=sha256(outside / "compatible.json"),
                    compatible_package_zip=compatible_zip,
                    compatible_release_tag="fw-packages-dev-004",
                    catalog_release_tag="fw-packages-dev-005",
                )

    def test_package_release_can_target_existing_dev_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            target_manifest["firmware"]["version"] = "t-dev-004-013"
            target_manifest.pop("release_id")
            target_manifest["release_id"] = manifest_release_id(target_manifest)
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")

            manifest = build_package_release(
                repo,
                build,
                repo / "dist/f7-C/f7-update-t-dev-004-013",
                target_release_tag="t-dev-004-013",
                target_manifest=target_manifest,
                target_package_zip=target_zip,
            )

            self.assertEqual(manifest["firmware"], target_manifest["firmware"])
            self.assertEqual(
                manifest["package_release"]["target_release_tag"],
                "t-dev-004-013",
            )
            self.assertEqual(
                manifest["package_release"]["target_release_id"],
                target_manifest["release_id"],
            )
            self.assertTrue(
                manifest["package_release"]["firmware_flash_unchanged"]
            )

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

    def test_independent_catalog_has_its_own_channel_revision_and_tag(self) -> None:
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
                catalog_release_tag="fw-packages-stable-001",
            )

            release = manifest["package_release"]
            self.assertEqual(release["id"], "fw-packages-stable-001")
            self.assertEqual(release["catalog_channel"], "stable")
            self.assertEqual(release["catalog_revision"], 1)
            self.assertEqual(
                release["catalog_release_tag"],
                "fw-packages-stable-001",
            )
            self.assertEqual(release["target_release_tag"], "v1.0.4")
            unsigned = dict(manifest)
            unsigned.pop("release_id")
            self.assertEqual(manifest["release_id"], manifest_release_id(unsigned))

    def test_independent_catalog_rejects_bad_tag_or_channel_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo, build, resources = prepare_package_tree(Path(directory))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")

            for tag in ("fw-packages-stable-000", "fw-packages-beta-001", "v1.0.4"):
                with self.subTest(tag=tag), self.assertRaises(ValidationError):
                    build_package_release(
                        repo,
                        build,
                        repo / "dist/out",
                        target_release_tag="v1.0.4",
                        target_manifest=target_manifest,
                        target_package_zip=target_zip,
                        catalog_release_tag=tag,
                    )

            with self.assertRaisesRegex(ValidationError, "does not match target"):
                build_package_release(
                    repo,
                    build,
                    repo / "dist/out",
                    target_release_tag="v1.0.4",
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                    catalog_release_tag="fw-packages-dev-001",
                )

    def test_independent_dev_catalog_requires_accepted_dev_baseline(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            repo, build, resources = prepare_package_tree(Path(temp))
            target_manifest, target_zip = prepare_target_package(repo, resources)
            target_manifest["firmware"]["version"] = "t-dev-004-013"
            target_manifest.pop("release_id")
            target_manifest["release_id"] = manifest_release_id(target_manifest)
            write_file(build / ".extapps/esp_flasher.fap", b"protected esp flasher")

            accepted = build_package_release(
                repo,
                build,
                repo / "accepted",
                target_release_tag="t-dev-004-013",
                target_manifest=target_manifest,
                target_package_zip=target_zip,
                catalog_release_tag="fw-packages-dev-002",
            )
            self.assertEqual(
                accepted["package_release"]["target_release_tag"],
                "t-dev-004-013",
            )

            with self.assertRaisesRegex(
                ValidationError,
                "Catalog dev baseline must be t-dev-004-013",
            ):
                build_package_release(
                    repo,
                    build,
                    repo / "rejected",
                    target_release_tag="t-dev-004-014",
                    target_manifest=target_manifest,
                    target_package_zip=target_zip,
                    catalog_release_tag="fw-packages-dev-003",
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

    def test_existing_stable_release_overlays_only_audited_targets(self) -> None:
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
            raw_source = "apps/ARF Tools/subghz_raw_edit.fap"
            totp_plugin_source = TOTP_CLI_PLUGIN_PACKAGE_FILES[0]
            write_file(build / ".extapps/subghz_raw_edit.fap", b"accepted raw edit")
            write_file(
                build / ".extapps" / Path(totp_plugin_source).name,
                b"accepted totp plugin",
            )

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
            overlay_targets = {f"/ext/{source}" for source in PACKAGE_RELEASE_OVERLAY_FILES}
            self.assertEqual(
                {
                    key: value
                    for key, value in new_entries.items()
                    if key not in overlay_targets
                },
                {
                    key: value
                    for key, value in old_entries.items()
                    if key not in overlay_targets
                },
            )
            self.assertEqual(
                set(new_entries) - set(old_entries),
                {flasher_target},
            )
            self.assertEqual(manifest["artifacts"], target_manifest["artifacts"])
            self.assertEqual(manifest["safety"], target_manifest["safety"])
            self.assertEqual(
                manifest["package_release"]["overlay_targets"],
                sorted(PACKAGE_RELEASE_OVERLAY_FILES),
            )
            self.assertEqual(
                new_entries[f"/ext/{raw_source}"]["md5"],
                md5(build / ".extapps/subghz_raw_edit.fap"),
            )
            self.assertEqual(
                new_entries[f"/ext/{totp_plugin_source}"]["md5"],
                md5(build / ".extapps" / Path(totp_plugin_source).name),
            )

            output_zip = repo / "dist/f7-C/f7-update-t-flppr-fw-004/tumoflip-packages.zip"
            with zipfile.ZipFile(target_zip) as old_archive, zipfile.ZipFile(output_zip) as new_archive:
                for name in old_archive.namelist():
                    if name not in PACKAGE_RELEASE_OVERLAY_FILES:
                        self.assertEqual(new_archive.read(name), old_archive.read(name))
                self.assertEqual(
                    new_archive.read(flasher_source),
                    b"protected esp flasher",
                )
                self.assertEqual(new_archive.read(raw_source), b"accepted raw edit")
                self.assertEqual(
                    new_archive.read(totp_plugin_source),
                    b"accepted totp plugin",
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
