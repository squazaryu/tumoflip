import io
import hashlib
import json
import struct
import subprocess
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from typing import Optional
from unittest import mock

import heatshrink2

from tools.tumoflip import protected_app_audit as audit


REPO_ROOT = Path(__file__).resolve().parents[2]
REGISTRY_PATH = REPO_ROOT / "tools/tumoflip/protected_apps_registry.json"
RAW_EDIT_HEAD = "d60ee2a34fa87b89c99f1ba9056737765f9f921f"
RAW_EDIT_DECISIONS = {
    "9aug2026": "0b71d9f34fec8ae3ba763b8de27ef15d1d604c5b",
    "12aug2026": "585b144ac5b4d9a48a0e5a74570a6584353fdbba",
}


class ProtectedAppAuditTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repo = self.root / "source"
        self.repo.mkdir()
        subprocess.run(["git", "init", "-q"], cwd=self.repo, check=True)
        subprocess.run(
            ["git", "config", "user.email", "audit@example.invalid"],
            cwd=self.repo,
            check=True,
        )
        subprocess.run(
            ["git", "config", "user.name", "Audit Test"],
            cwd=self.repo,
            check=True,
        )
        self.registry = audit.read_json(REGISTRY_PATH)
        self.apps = audit.validate_registry(self.registry)
        for app in self.apps:
            source = self.repo / app["packSourcePath"]
            source.mkdir(parents=True, exist_ok=True)
            (source / "source.txt").write_text(app["id"], encoding="utf-8")
        subprocess.run(["git", "add", "."], cwd=self.repo, check=True)
        subprocess.run(["git", "commit", "-qm", "baseline"], cwd=self.repo, check=True)
        self.before = self.git("rev-parse", "HEAD")
        (self.repo / "unrelated.txt").write_text("next", encoding="utf-8")
        subprocess.run(["git", "add", "."], cwd=self.repo, check=True)
        subprocess.run(["git", "commit", "-qm", "next"], cwd=self.repo, check=True)
        self.after = self.git("rev-parse", "HEAD")

        self.base = self.root / "all-the-apps-base.zip"
        self.extra = self.root / "all-the-apps-extra.zip"
        self._write_archives()
        self.author_heads = self.root / "author-heads.json"
        heads = {
            app["id"]: app["author"]["lastReviewedCommit"]
            for app in self.apps
            if app["author"]["ref"] != "release-source"
        }
        heads["subghz_raw_edit"] = RAW_EDIT_HEAD
        self.author_heads.write_text(json.dumps({"heads": heads}), encoding="utf-8")
        self.stable_manifest = self._write_target_manifest(
            "stable", "fw-packages-stable-001", "a"
        )
        self.dev_manifest = self._write_target_manifest("dev", "fw-packages-dev-003", "b")
        self.stable_archive = self._write_target_archive(
            "stable", "fw-packages-stable-001", "a"
        )
        self.dev_archive = self._write_target_archive("dev", "fw-packages-dev-003", "b")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def git(self, *args: str) -> str:
        return subprocess.run(
            ["git", *args],
            cwd=self.repo,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

    def _write_archives(self, *, omit: Optional[str] = None, add_unknown: bool = False) -> None:
        members: dict[str, dict[str, bytes]] = {"base": {}, "extra": {}}
        for app in self.apps:
            for spec in app["artifacts"]:
                if spec["archivePath"] != omit:
                    members[spec["pack"]][spec["archivePath"]] = spec["remotePath"].encode()
            family = app.get("artifactFamily")
            if family:
                for index in range(family["expectedCount"]):
                    name = f"totp_cli_{index:02d}{family['extension']}"
                    path = family["archivePrefix"] + name
                    if path != omit:
                        members[family["pack"]][path] = name.encode()
        if add_unknown:
            members["extra"][
                "extra_pack_build/artifacts-extra/Tools/field_logger.fap"
            ] = b"new protected intersection"
        for pack, path in (("base", self.base), ("extra", self.extra)):
            with zipfile.ZipFile(path, "w") as archive:
                for name, data in sorted(members[pack].items()):
                    archive.writestr(name, data)

    def _args(self, *, decisions: Optional[Path] = None) -> SimpleNamespace:
        return SimpleNamespace(
            repo=self.repo,
            implementation_repo=self.repo,
            registry=REGISTRY_PATH,
            base_archive=self.base,
            extra_archive=self.extra,
            base_sha256=audit.file_hash(self.base, "sha256"),
            extra_sha256=audit.file_hash(self.extra, "sha256"),
            source_tag="test-release",
            source_commit=self.after,
            previous_source_commit=self.before,
            release_url="https://example.invalid/release",
            published_at="2026-08-09T00:00:00Z",
            api="88.2",
            sequence=1,
            issue_number=302,
            issue_url="https://github.com/squazaryu/tumoflip/issues/302",
            decisions=decisions,
            author_heads=self.author_heads,
            target_manifest=[self.stable_manifest, self.dev_manifest],
            target_archive=[self.stable_archive, self.dev_archive],
            firmware_updater_descriptor=[],
            generated_at="2026-08-12T00:00:00+00:00",
        )

    def _write_firmware_updater(
        self,
        *,
        release_tag: str = "t-dev-004-015",
        firmware_version: str = "t-dev-004-015",
        targets: Optional[dict[str, bytes]] = None,
        manifest_data: Optional[dict[str, bytes]] = None,
        target: str = "7",
    ) -> Path:
        targets = targets or {}
        manifest_data = targets if manifest_data is None else manifest_data
        manifest_lines = ["V:0", "T:1"]
        for remote, data in sorted(manifest_data.items()):
            relative = remote.removeprefix("/ext/")
            manifest_lines.append(
                f"F:{hashlib.md5(data).hexdigest()}:{len(data)}:{relative}"
            )
        manifest = ("\n".join(manifest_lines) + "\n").encode()
        resource_tar = io.BytesIO()
        with tarfile.open(fileobj=resource_tar, mode="w:") as archive:
            all_files = {"Manifest": manifest}
            all_files.update(
                {remote.removeprefix("/ext/"): data for remote, data in targets.items()}
            )
            for name, data in sorted(all_files.items()):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        resources = struct.pack("<IBBB", 0x53445348, 1, 13, 6) + heatshrink2.compress(
            resource_tar.getvalue(), window_sz2=13, lookahead_sz2=6
        )
        root = f"f7-update-{firmware_version}"
        fuf = (
            "Filetype: Flipper firmware upgrade configuration\n"
            "Version: 2\n"
            f"Info: {firmware_version}\n"
            f"Target: {target}\n"
            "Resources: resources.ths\n"
        ).encode()
        archive_path = self.root / f"flipper-z-f7-update-{firmware_version}.tgz"
        with tarfile.open(archive_path, mode="w:gz") as archive:
            for name, data in ((f"{root}/update.fuf", fuf), (f"{root}/resources.ths", resources)):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        descriptor = self.root / f"firmware-{release_tag}.json"
        descriptor.write_text(
            json.dumps(
                {
                    "schema": 1,
                    "releaseTag": release_tag,
                    "releaseCommit": self.after,
                    "assetFileName": archive_path.name,
                    "assetSHA256": audit.file_hash(archive_path, "sha256"),
                }
            ),
            encoding="utf-8",
        )
        return descriptor

    def _write_target_manifest(self, channel: str, release_tag: str, seed: str) -> Path:
        data = seed.encode()
        entries = []
        for app in self.apps:
            for spec in app["artifacts"]:
                if app["defaultDisposition"] == "intentionallyReplaced":
                    continue
                entries.append(
                    {
                        "target": spec["targetPath"],
                        "md5": hashlib.md5(data).hexdigest(),
                        "sha256": hashlib.sha256(data).hexdigest(),
                        "bytes": len(data),
                    }
                )
        revision = int(release_tag.rsplit("-", 1)[1])
        path = self.root / f"{release_tag}.json"
        path.write_text(
            json.dumps(
                {
                    "schema": 2,
                    "package_release": {
                        "catalog_channel": channel,
                        "catalog_release_tag": release_tag,
                        "catalog_revision": revision,
                        "source_commit": self.after,
                        "source_dirty": False,
                        "target_release_tag": f"target-{channel}",
                        "target_release_id": seed * 64,
                    },
                    "packages": {"protected": entries},
                }
            ),
            encoding="utf-8",
        )
        return path

    def _write_target_archive(self, channel: str, release_tag: str, seed: str) -> Path:
        path = self.root / f"{channel}:{release_tag}=targets.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for app in self.apps:
                if app["defaultDisposition"] == "intentionallyReplaced":
                    continue
                for spec in app["artifacts"]:
                    archive.writestr(spec["targetPath"].removeprefix("/ext/"), seed.encode())
        return path

    def _retain_compatible_target_build(
        self, manifest_path: Path, previous_manifest_path: Path, target: str
    ) -> None:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
        previous_document = json.loads(previous_manifest_path.read_text(encoding="utf-8"))
        previous_release = previous_document["package_release"]
        document["package_release"]["compatible_releases"] = [
            {
                "release_tag": previous_release["catalog_release_tag"],
                "release_id": previous_document["release_id"],
                "manifest_sha256": audit.file_hash(previous_manifest_path, "sha256"),
                "source_commit": previous_release["source_commit"],
            }
        ]
        previous_entry = next(
            item for item in previous_document["packages"]["protected"]
            if item["target"] == target
        )
        current_entry = next(
            item for item in document["packages"]["protected"]
            if item["target"] == target
        )
        current_entry["compatible_builds"] = [
            {
                "release_id": previous_document["release_id"],
                "md5": previous_entry["md5"],
                "sha256": previous_entry["sha256"],
                "bytes": previous_entry["bytes"],
            }
        ]
        document["release_id"] = audit.manifest_release_id(document)
        manifest_path.write_text(json.dumps(document), encoding="utf-8")

    def _add_totp_target_family(self) -> None:
        family = next(app["artifactFamily"] for app in self.apps if app["id"] == "totp")
        for manifest_path, archive_path, seed in (
            (self.stable_manifest, self.stable_archive, "a"),
            (self.dev_manifest, self.dev_archive, "b"),
        ):
            document = json.loads(manifest_path.read_text(encoding="utf-8"))
            entries = document["packages"]["protected"]
            retained: dict[str, bytes] = {}
            with zipfile.ZipFile(archive_path) as archive:
                retained = {
                    info.filename: archive.read(info)
                    for info in archive.infolist()
                    if not info.is_dir()
                }
            data = seed.encode()
            for index in range(family["expectedCount"]):
                filename = f"totp_cli_{index:02d}{family['extension']}"
                target = family["targetPrefix"] + filename
                entries.append(
                    {
                        "target": target,
                        "md5": hashlib.md5(data).hexdigest(),
                        "sha256": hashlib.sha256(data).hexdigest(),
                        "bytes": len(data),
                    }
                )
                retained[target.removeprefix("/ext/")] = data
            manifest_path.write_text(json.dumps(document), encoding="utf-8")
            with zipfile.ZipFile(archive_path, "w") as archive:
                for name, content in sorted(retained.items()):
                    archive.writestr(name, content)

    def _make_raw_target_match_source(self) -> None:
        raw = next(app for app in self.apps if app["id"] == "subghz_raw_edit")
        spec = raw["artifacts"][0]
        with zipfile.ZipFile(self.extra) as archive:
            source_data = archive.read(spec["archivePath"])
        for manifest_path, archive_path in (
            (self.stable_manifest, self.stable_archive),
            (self.dev_manifest, self.dev_archive),
        ):
            document = json.loads(manifest_path.read_text(encoding="utf-8"))
            entry = next(
                value
                for value in document["packages"]["protected"]
                if value["target"] == spec["targetPath"]
            )
            entry.update(
                {
                    "md5": hashlib.md5(source_data).hexdigest(),
                    "sha256": hashlib.sha256(source_data).hexdigest(),
                    "bytes": len(source_data),
                }
            )
            manifest_path.write_text(json.dumps(document), encoding="utf-8")
            with zipfile.ZipFile(archive_path) as archive:
                retained = {
                    info.filename: archive.read(info)
                    for info in archive.infolist()
                    if not info.is_dir()
                }
            retained[spec["targetPath"].removeprefix("/ext/")] = source_data
            with zipfile.ZipFile(archive_path, "w") as archive:
                for name, content in sorted(retained.items()):
                    archive.writestr(name, content)

    def _set_raw_author_head(self, commit: str) -> None:
        document = json.loads(self.author_heads.read_text(encoding="utf-8"))
        document["heads"]["subghz_raw_edit"] = commit
        self.author_heads.write_text(json.dumps(document), encoding="utf-8")

    def test_changed_raw_edit_is_explicitly_unresolved_and_omitted(self) -> None:
        self._set_raw_author_head("e" * 40)
        result, _ = audit.audit_release(self._args())

        self.assertEqual(result["overallStatus"], "pending")
        self.assertEqual(len(result["entries"]), 9)
        self.assertEqual(len(result["unresolved"]), 15)
        self.assertTrue(
            any(
                value.startswith("subghz_raw_edit:/ext/apps/Sub-GHz/subghz_raw_edit.fap")
                for value in result["unresolved"]
            )
        )
        self.assertFalse(
            any(entry["remotePath"].endswith("subghz_raw_edit.fap") for entry in result["entries"])
        )
        raw = next(app for app in result["apps"] if app["appId"] == "subghz_raw_edit")
        self.assertEqual(raw["status"], "needsReview")
        self.assertEqual(sum(len(app["artifacts"]) for app in result["apps"]), 24)
        accepted = next(
            entry for entry in result["entries"]
            if entry["remotePath"].endswith("esp32_wifi_marauder.fap")
        )
        self.assertEqual(
            accepted["targetMD5s"],
            sorted(
                {
                    hashlib.md5(b"a").hexdigest(),
                    hashlib.md5(b"b").hexdigest(),
                }
            ),
        )
        self.assertEqual(len(accepted["targetProvenance"]), 2)
        replaced = next(
            entry for entry in result["entries"]
            if entry["disposition"] == "intentionallyReplaced"
        )
        self.assertEqual(replaced["targetMD5s"], [])
        self.assertEqual(replaced["targetProvenance"], [])

    def test_exact_hardware_accepted_decision_resolves_changed_app(self) -> None:
        decisions = self.root / "decisions.json"
        decisions.write_text(
            json.dumps(
                {
                    "schema": 2,
                    "decisions": [
                        {
                            "appId": "subghz_raw_edit",
                            "throughAuthorCommit": RAW_EDIT_HEAD,
                            "sourceCommit": self.after,
                            "disposition": "auditedDifference",
                            "changelog": "Port NumberInput and zero-gap merge.",
                            "implementationCommit": self.after,
                            "fwPackages": {
                                "channel": "dev",
                                "revision": 3,
                                "releaseTag": "fw-packages-dev-003",
                            },
                            "hardwareAccepted": True,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        result, _ = audit.audit_release(self._args(decisions=decisions))

        self.assertEqual(result["overallStatus"], "pending")
        self.assertEqual(len(result["entries"]), 10)
        self.assertEqual(len(result["unresolved"]), 14)

    def test_checked_in_raw_edit_decisions_accept_the_exact_live_sources(self) -> None:
        previous_manifest = self._write_target_manifest(
            "dev", "fw-packages-dev-004", "c"
        )
        previous_document = json.loads(previous_manifest.read_text(encoding="utf-8"))
        previous_document["release_id"] = audit.manifest_release_id(previous_document)
        previous_manifest.write_text(json.dumps(previous_document), encoding="utf-8")

        dev_manifest = self._write_target_manifest("dev", "fw-packages-dev-005", "d")
        dev_archive = self._write_target_archive("dev", "fw-packages-dev-005", "d")
        raw_target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "subghz_raw_edit"
        )
        esp_target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "esp_flasher"
        )
        dev_document = json.loads(dev_manifest.read_text(encoding="utf-8"))
        dev_document["package_release"]["compatible_releases"] = [
            {
                "release_tag": "fw-packages-dev-004",
                "release_id": previous_document["release_id"],
                "manifest_sha256": audit.file_hash(previous_manifest, "sha256"),
                "source_commit": self.after,
            }
        ]
        previous_entries = {
            entry["target"]: entry
            for entry in previous_document["packages"]["protected"]
        }
        for entry in dev_document["packages"]["protected"]:
            if entry["target"] not in {raw_target, esp_target}:
                continue
            previous = previous_entries[entry["target"]]
            entry["compatible_builds"] = [
                {
                    "release_id": previous_document["release_id"],
                    "md5": previous["md5"],
                    "sha256": previous["sha256"],
                    "bytes": previous["bytes"],
                }
            ]
        dev_document["release_id"] = audit.manifest_release_id(dev_document)
        dev_manifest.write_text(json.dumps(dev_document), encoding="utf-8")
        firmware = self._write_firmware_updater(targets={raw_target: b"firmware-raw"})

        for source_tag, source_commit in RAW_EDIT_DECISIONS.items():
            with self.subTest(source_tag=source_tag):
                decision_path = (
                    REPO_ROOT
                    / "tools/tumoflip/protected_audit_decisions"
                    / f"{source_tag}.json"
                )
                decisions = audit.load_decisions(decision_path)
                decision = decisions["subghz_raw_edit"]
                self.assertEqual(decision["sourceCommit"], source_commit)
                self.assertEqual(decision["throughAuthorCommit"], RAW_EDIT_HEAD)
                self.assertTrue(decision["hardwareAccepted"])

                args = self._args(decisions=decision_path)
                args.source_tag = source_tag
                args.source_commit = source_commit
                args.target_manifest = [self.stable_manifest, dev_manifest]
                args.target_archive = [self.stable_archive, dev_archive]
                args.firmware_updater_descriptor = [firmware]

                def raw_edit_changed(
                    _repo: Path, _before: str, _after: str, path: str
                ) -> bool:
                    return path == "applications_user/subghz_raw_edit"

                with (
                    mock.patch.object(audit, "git_path_changed", side_effect=raw_edit_changed),
                    mock.patch.object(audit, "git_commit_is_ancestor", return_value=True),
                ):
                    result, _ = audit.audit_release(args)

                raw_app = next(
                    app for app in result["apps"] if app["appId"] == "subghz_raw_edit"
                )
                raw_entry = next(
                    entry
                    for entry in result["entries"]
                    if entry["remotePath"].endswith("subghz_raw_edit.fap")
                )
                self.assertEqual(raw_app["status"], "verified")
                self.assertEqual(raw_app["decision"], decision)
                self.assertEqual(raw_entry["disposition"], "auditedDifference")
                self.assertEqual(
                    {item["releaseTag"] for item in raw_entry["targetProvenance"]},
                    {"fw-packages-dev-004", "fw-packages-dev-005"},
                )
                self.assertEqual(
                    raw_entry["targetMD5s"],
                    sorted(
                        {
                            hashlib.md5(b"c").hexdigest(),
                            hashlib.md5(b"d").hexdigest(),
                        }
                    ),
                )
                self.assertIn(
                    "fwPackagesCompatibleBuild",
                    {item["containerKind"] for item in raw_entry["targetProvenance"]},
                )
                self.assertNotIn(
                    "firmwareUpdaterBundle",
                    {item["containerKind"] for item in raw_entry["targetProvenance"]},
                )

                unchanged_entry = next(
                    entry
                    for entry in result["entries"]
                    if entry["remotePath"].endswith("esp_flasher.fap")
                )
                self.assertEqual(
                    {item["releaseTag"] for item in unchanged_entry["targetProvenance"]},
                    {
                        "fw-packages-stable-001",
                        "fw-packages-dev-004",
                        "fw-packages-dev-005",
                    },
                )
                self.assertEqual(
                    unchanged_entry["targetMD5s"],
                    sorted(
                        {
                            hashlib.md5(b"a").hexdigest(),
                            hashlib.md5(b"c").hexdigest(),
                            hashlib.md5(b"d").hexdigest(),
                        }
                    ),
                )

    def test_compatible_target_build_requires_content_addressed_manifest(self) -> None:
        manifest = self._write_target_manifest("dev", "fw-packages-dev-005", "d")
        document = json.loads(manifest.read_text(encoding="utf-8"))
        document["package_release"]["compatible_releases"] = [
            {
                "release_tag": "fw-packages-dev-004",
                "release_id": "1" * 64,
                "manifest_sha256": "2" * 64,
                "source_commit": self.after,
            }
        ]
        document["packages"]["protected"][0]["compatible_builds"] = [
            {
                "release_id": "1" * 64,
                "md5": hashlib.md5(b"old").hexdigest(),
                "sha256": hashlib.sha256(b"old").hexdigest(),
                "bytes": 3,
            }
        ]
        document["release_id"] = "f" * 64
        manifest.write_text(json.dumps(document), encoding="utf-8")

        with self.assertRaisesRegex(audit.AuditError, "release id differs"):
            audit.load_target_manifests([manifest])

    def test_live_like_catalog_chain_keeps_one_latest_client_provenance(self) -> None:
        target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "esp_flasher"
        )
        previous_manifest = self._write_target_manifest(
            "dev", "fw-packages-dev-004", "c"
        )
        previous_document = json.loads(previous_manifest.read_text(encoding="utf-8"))
        previous_document["release_id"] = audit.manifest_release_id(previous_document)
        previous_manifest.write_text(json.dumps(previous_document), encoding="utf-8")

        manifests: list[Path] = []
        archives: list[Path] = []
        for revision, seed in ((5, "d"), (7, "e"), (8, "f")):
            tag = f"fw-packages-dev-{revision:03d}"
            manifest = self._write_target_manifest("dev", tag, seed)
            archive = self._write_target_archive("dev", tag, seed)
            self._retain_compatible_target_build(manifest, previous_manifest, target)
            manifests.append(manifest)
            archives.append(archive)

        args = self._args()
        args.target_manifest = [self.stable_manifest, *manifests]
        args.target_archive = [self.stable_archive, *archives]
        result, _ = audit.audit_release(args)

        entry = next(item for item in result["entries"] if item["targetPath"] == target)
        retained = [
            item for item in entry["targetProvenance"]
            if item["releaseTag"] == "fw-packages-dev-004"
        ]
        self.assertEqual(len(retained), 1)
        self.assertEqual(retained[0]["compatibilityCatalogTag"], "fw-packages-dev-008")
        # Exact fields decoded by TumoCompanion 1.10.27. Keep this independent
        # from the generator helper so a shared implementation bug cannot make
        # the regression pass tautologically.
        identities = [
            tuple(
                item[field]
                for field in ("targetMD5", "channel", "releaseTag", "manifestSHA256")
            )
            for item in entry["targetProvenance"]
        ]
        self.assertEqual(len(identities), len(set(identities)))
        audit.validate_audit(result)

    def test_validator_enforces_v11027_client_provenance_identity(self) -> None:
        result, _ = audit.audit_release(self._args())
        entry = next(item for item in result["entries"] if item["targetProvenance"])
        duplicate = dict(entry["targetProvenance"][0])
        duplicate["containerSHA256"] = "f" * 64
        entry["targetProvenance"].append(duplicate)

        with self.assertRaisesRegex(
            audit.AuditError, "duplicate TumoCompanion target provenance"
        ):
            audit.validate_audit(result)

    def test_exact_targets_for_every_artifact_can_verify_release(self) -> None:
        self._add_totp_target_family()
        decisions = self.root / "decisions.json"
        decisions.write_text(
            json.dumps(
                {
                    "schema": 2,
                    "decisions": [
                        {
                            "appId": "subghz_raw_edit",
                            "throughAuthorCommit": RAW_EDIT_HEAD,
                            "sourceCommit": self.after,
                            "disposition": "auditedDifference",
                            "changelog": "Accepted port.",
                            "implementationCommit": self.after,
                            "fwPackages": {
                                "channel": "dev",
                                "revision": 3,
                                "releaseTag": "fw-packages-dev-003",
                            },
                            "hardwareAccepted": True,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        result, _ = audit.audit_release(self._args(decisions=decisions))

        self.assertEqual(result["overallStatus"], "verified")
        self.assertEqual(len(result["entries"]), 24)
        self.assertEqual(result["unresolved"], [])

    def test_changed_app_decision_requires_hardware_acceptance(self) -> None:
        decisions = self.root / "decisions.json"
        decisions.write_text(
            json.dumps(
                {
                    "schema": 2,
                    "decisions": [
                        {
                            "appId": "subghz_raw_edit",
                            "throughAuthorCommit": RAW_EDIT_HEAD,
                            "sourceCommit": self.after,
                            "disposition": "auditedDifference",
                            "changelog": "Unaccepted port.",
                            "implementationCommit": self.after,
                            "fwPackages": {
                                "channel": "dev",
                                "revision": 3,
                                "releaseTag": "fw-packages-dev-003",
                            },
                            "hardwareAccepted": False,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        with self.assertRaisesRegex(audit.AuditError, "hardwareAccepted=true"):
            audit.audit_release(self._args(decisions=decisions))

    def test_rejected_source_change_keeps_exact_existing_target(self) -> None:
        decisions = self.root / "decisions.json"
        decisions.write_text(
            json.dumps(
                {
                    "schema": 2,
                    "decisions": [
                        {
                            "appId": "subghz_raw_edit",
                            "throughAuthorCommit": RAW_EDIT_HEAD,
                            "sourceCommit": self.after,
                            "disposition": "rejected",
                            "changelog": "Rejected upstream UI change by design.",
                            "fwPackages": {
                                "channel": "dev",
                                "revision": 3,
                                "releaseTag": "fw-packages-dev-003",
                            },
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        result, _ = audit.audit_release(self._args(decisions=decisions))

        raw_entry = next(
            item for item in result["entries"] if item["remotePath"].endswith("subghz_raw_edit.fap")
        )
        self.assertEqual(raw_entry["disposition"], "auditedDifference")
        self.assertEqual(raw_entry["targetMD5s"], [hashlib.md5(b"b").hexdigest()])
        self.assertEqual(
            {item["releaseTag"] for item in raw_entry["targetProvenance"]},
            {"fw-packages-dev-003"},
        )
        raw_app = next(item for item in result["apps"] if item["appId"] == "subghz_raw_edit")
        self.assertEqual(raw_app["decisionDisposition"], "rejected")

    def test_intentionally_replaced_target_must_be_absent_from_exact_packages(self) -> None:
        claude = next(app for app in self.apps if app["id"] == "claude_buddy")
        target = claude["artifacts"][0]["targetPath"]
        data = b"stale replaced app"
        for manifest_path, archive_path in (
            (self.stable_manifest, self.stable_archive),
            (self.dev_manifest, self.dev_archive),
        ):
            document = json.loads(manifest_path.read_text(encoding="utf-8"))
            document["packages"]["protected"].append(
                {
                    "target": target,
                    "md5": hashlib.md5(data).hexdigest(),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "bytes": len(data),
                }
            )
            manifest_path.write_text(json.dumps(document), encoding="utf-8")
            with zipfile.ZipFile(archive_path) as archive:
                retained = {
                    info.filename: archive.read(info)
                    for info in archive.infolist()
                    if not info.is_dir()
                }
            retained[target.removeprefix("/ext/")] = data
            with zipfile.ZipFile(archive_path, "w") as archive:
                for name, content in sorted(retained.items()):
                    archive.writestr(name, content)

        result, _ = audit.audit_release(self._args())

        self.assertFalse(
            any(item["remotePath"].endswith("claude_remote_ble.fap") for item in result["entries"])
        )
        self.assertTrue(
            any("intentionally replaced target is still shipped" in value for value in result["unresolved"])
        )

    def test_source_matches_requires_exact_target_bytes(self) -> None:
        self._make_raw_target_match_source()
        decisions = self.root / "decisions.json"
        decisions.write_text(
            json.dumps(
                {
                    "schema": 2,
                    "decisions": [
                        {
                            "appId": "subghz_raw_edit",
                            "throughAuthorCommit": RAW_EDIT_HEAD,
                            "sourceCommit": self.after,
                            "disposition": "sourceMatches",
                            "changelog": "Exact accepted source and target bytes.",
                            "fwPackages": {
                                "channel": "dev",
                                "revision": 3,
                                "releaseTag": "fw-packages-dev-003",
                            },
                            "hardwareAccepted": True,
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )

        result, _ = audit.audit_release(self._args(decisions=decisions))

        raw_entry = next(
            item for item in result["entries"] if item["remotePath"].endswith("subghz_raw_edit.fap")
        )
        self.assertEqual(raw_entry["disposition"], "sourceMatches")
        self.assertEqual(raw_entry["targetMD5s"], [raw_entry["sourceMD5"]])
        self.assertEqual(
            {item["releaseTag"] for item in raw_entry["targetProvenance"]},
            {"fw-packages-dev-003"},
        )

    def test_archive_digest_mismatch_fails_closed(self) -> None:
        args = self._args()
        args.base_sha256 = "0" * 64
        with self.assertRaisesRegex(audit.AuditError, "SHA-256 differs"):
            audit.audit_release(args)

    def test_missing_protected_family_member_fails_closed(self) -> None:
        family = next(app["artifactFamily"] for app in self.apps if app["id"] == "totp")
        self._write_archives(omit=family["archivePrefix"] + "totp_cli_00.fal")
        with self.assertRaisesRegex(audit.AuditError, "artifact family differs"):
            audit.audit_release(self._args())

    def test_unregistered_protected_alias_fails_closed(self) -> None:
        self._write_archives(add_unknown=True)
        with self.assertRaisesRegex(audit.AuditError, "unregistered protected artifact"):
            audit.audit_release(self._args())

    def test_merge_preserves_pinned_release_and_replaces_exact_audit(self) -> None:
        first, _ = audit.audit_release(self._args())
        older = json.loads(json.dumps(first))
        older["sourceTag"] = "older"
        older["sequence"] = 1
        older["archives"][0]["sha256"] = "1" * 64
        older["archives"][1]["sha256"] = "2" * 64
        ledger = audit.merge_ledger(None, older)
        ledger = audit.merge_ledger(ledger, first)
        before_noop = json.loads(json.dumps(ledger))
        updated = json.loads(json.dumps(first))
        updated["generatedAt"] = "2026-08-13T00:00:00+00:00"
        ledger = audit.merge_ledger(ledger, updated)

        self.assertEqual(ledger, before_noop)
        updated["entries"][0]["note"] += " Reviewed again."
        updated["apps"][0]["note"] += " Reviewed again."
        ledger = audit.merge_ledger(ledger, updated)

        self.assertEqual(ledger["schema"], 2)
        self.assertEqual(ledger["sourceRepository"], "xMasterX/all-the-plugins")
        self.assertEqual([item["sourceTag"] for item in ledger["audits"]], ["older", "test-release"])
        self.assertEqual(ledger["audits"][1]["generatedAt"], updated["generatedAt"])

    def test_merge_normalizes_legacy_client_duplicate_provenance(self) -> None:
        current, _ = audit.audit_release(self._args())
        older = json.loads(json.dumps(current))
        older["sourceTag"] = "older"
        older["archives"][0]["sha256"] = "1" * 64
        older["archives"][1]["sha256"] = "2" * 64
        entry = next(item for item in older["entries"] if item["targetProvenance"])
        duplicate = dict(entry["targetProvenance"][0])
        duplicate["containerSHA256"] = "f" * 64
        entry["targetProvenance"].append(duplicate)
        legacy = {
            "schema": 2,
            "sourceRepository": "xMasterX/all-the-plugins",
            "generatedAt": older["generatedAt"],
            "audits": [older],
        }

        with self.assertRaisesRegex(
            audit.AuditError, "duplicate TumoCompanion target provenance"
        ):
            audit.validate_ledger(legacy)

        merged = audit.merge_ledger(legacy, current)

        normalized_older = next(
            item for item in merged["audits"] if item["sourceTag"] == "older"
        )
        normalized_entry = next(
            item
            for item in normalized_older["entries"]
            if item["targetPath"] == entry["targetPath"]
        )
        identities = [
            tuple(
                item[field]
                for field in ("targetMD5", "channel", "releaseTag", "manifestSHA256")
            )
            for item in normalized_entry["targetProvenance"]
        ]
        self.assertEqual(len(identities), len(set(identities)))
        audit.validate_ledger(merged)

    def test_merge_does_not_hide_invalid_legacy_duplicate_provenance(self) -> None:
        current, _ = audit.audit_release(self._args())
        entry = next(item for item in current["entries"] if item["targetProvenance"])
        invalid_duplicate = dict(entry["targetProvenance"][0])
        invalid_duplicate.pop("targetReleaseTag")
        entry["targetProvenance"].append(invalid_duplicate)
        legacy = {
            "schema": 2,
            "sourceRepository": "xMasterX/all-the-plugins",
            "generatedAt": current["generatedAt"],
            "audits": [current],
        }

        with self.assertRaisesRegex(
            audit.AuditError, "target provenance targetReleaseTag"
        ):
            audit.merge_ledger(legacy, current)

    def test_legacy_provenance_shuffle_normalizes_to_identical_payload(self) -> None:
        current, _ = audit.audit_release(self._args())
        first = {
            "schema": 2,
            "sourceRepository": "xMasterX/all-the-plugins",
            "generatedAt": current["generatedAt"],
            "audits": [current],
        }
        entry = next(
            item
            for item in first["audits"][0]["entries"]
            if len(item["targetProvenance"]) >= 2
        )
        duplicate = dict(entry["targetProvenance"][0])
        duplicate["containerSHA256"] = "f" * 64
        entry["targetProvenance"].append(duplicate)
        shuffled = json.loads(json.dumps(first))
        shuffled_entry = next(
            item
            for item in shuffled["audits"][0]["entries"]
            if item["targetPath"] == entry["targetPath"]
        )
        shuffled_entry["targetProvenance"].reverse()

        first_normalized = audit.normalize_ledger_target_provenance(first)
        shuffled_normalized = audit.normalize_ledger_target_provenance(shuffled)

        self.assertEqual(first_normalized, shuffled_normalized)
        audit.validate_ledger(first_normalized)

    def test_same_pack_is_reaudited_when_target_release_changes(self) -> None:
        self._set_raw_author_head("e" * 40)
        first, _ = audit.audit_release(self._args())
        self.assertEqual(len(first["entries"]), 9)
        self.assertEqual(len(first["unresolved"]), 15)
        ledger = audit.merge_ledger(None, first)

        self._add_totp_target_family()
        second, _ = audit.audit_release(self._args())
        self.assertEqual(len(second["entries"]), 23)
        self.assertEqual(len(second["unresolved"]), 1)
        ledger = audit.merge_ledger(ledger, second)

        self.assertEqual(len(ledger["audits"]), 1)
        self.assertEqual(len(ledger["audits"][0]["entries"]), 23)
        self.assertEqual(len(ledger["audits"][0]["unresolved"]), 1)

    def test_semantic_identity_ignores_time_but_changes_with_target_evidence(self) -> None:
        first, _ = audit.audit_release(self._args())
        time_only = json.loads(json.dumps(first))
        time_only["generatedAt"] = "2026-08-13T00:00:00+00:00"
        self.assertEqual(
            audit.semantic_audit_sha256(first),
            audit.semantic_audit_sha256(time_only),
        )

        self._add_totp_target_family()
        targets_changed, _ = audit.audit_release(self._args())
        self.assertNotEqual(
            audit.semantic_audit_sha256(first),
            audit.semantic_audit_sha256(targets_changed),
        )

    def test_unresolved_disposition_cannot_be_published_as_entry(self) -> None:
        result, _ = audit.audit_release(self._args())
        result["entries"][0]["disposition"] = "needsReview"
        with self.assertRaisesRegex(audit.AuditError, "unresolved disposition"):
            audit.validate_audit(result)

    def test_target_provenance_must_cover_exact_target_md5_set(self) -> None:
        result, _ = audit.audit_release(self._args())
        entry = next(
            value for value in result["entries"]
            if value["disposition"] == "auditedDifference"
        )
        entry["targetMD5s"].append("c" * 32)
        with self.assertRaisesRegex(audit.AuditError, "hashes differ"):
            audit.validate_audit(result)

    def test_missing_target_from_every_manifest_remains_unresolved(self) -> None:
        for path in (self.stable_manifest, self.dev_manifest):
            document = json.loads(path.read_text(encoding="utf-8"))
            document["packages"]["protected"] = [
                item for item in document["packages"]["protected"]
                if not item["target"].endswith("esp32_wifi_marauder.fap")
            ]
            path.write_text(json.dumps(document), encoding="utf-8")
        for path in (self.stable_archive, self.dev_archive):
            retained: dict[str, bytes] = {}
            with zipfile.ZipFile(path) as archive:
                for info in archive.infolist():
                    if not info.filename.endswith("esp32_wifi_marauder.fap"):
                        retained[info.filename] = archive.read(info)
            with zipfile.ZipFile(path, "w") as archive:
                for name, data in retained.items():
                    archive.writestr(name, data)
        result, _ = audit.audit_release(self._args())

        self.assertFalse(
            any(
                item["remotePath"].endswith("esp32_wifi_marauder.fap")
                for item in result["entries"]
            )
        )
        self.assertTrue(
            any(
                "esp32_wifi_marauder" in value and "target absent" in value
                for value in result["unresolved"]
            )
        )

    def test_manifest_entry_must_match_exact_zip_bytes(self) -> None:
        document = json.loads(self.dev_manifest.read_text(encoding="utf-8"))
        document["packages"]["protected"][0]["md5"] = "f" * 32
        self.dev_manifest.write_text(json.dumps(document), encoding="utf-8")

        with self.assertRaisesRegex(audit.AuditError, "manifest/ZIP bytes differ"):
            audit.audit_release(self._args())

    def test_exact_firmware_updater_resources_add_target_provenance(self) -> None:
        target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "esp32_wifi_marauder"
        )
        # Use the same byte as dev FW Packages to prove that one target MD5 can
        # retain both exact provenances without widening the hash set.
        descriptor = self._write_firmware_updater(targets={target: b"b"})
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        result, _ = audit.audit_release(args)

        entry = next(item for item in result["entries"] if item["targetPath"] == target)
        self.assertEqual(
            entry["targetMD5s"],
            sorted({hashlib.md5(b"a").hexdigest(), hashlib.md5(b"b").hexdigest()}),
        )
        firmware = next(
            item
            for item in entry["targetProvenance"]
            if item["containerKind"] == "firmwareUpdaterBundle"
        )
        self.assertEqual(firmware["releaseTag"], "t-dev-004-015")
        self.assertEqual(firmware["firmwareVersion"], "t-dev-004-015")
        self.assertEqual(firmware["targetSourceCommit"], self.after)
        self.assertRegex(firmware["containerSHA256"], audit.HEX_64)
        self.assertRegex(firmware["manifestSHA256"], audit.HEX_64)
        self.assertRegex(firmware["resourcesSHA256"], audit.HEX_64)
        self.assertEqual(len(entry["targetProvenance"]), 3)
        audit.validate_audit(result)

    def test_firmware_updater_digest_mismatch_fails_closed(self) -> None:
        descriptor = self._write_firmware_updater()
        document = json.loads(descriptor.read_text(encoding="utf-8"))
        document["assetSHA256"] = "0" * 64
        descriptor.write_text(json.dumps(document), encoding="utf-8")
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        with self.assertRaisesRegex(audit.AuditError, "updater SHA-256 differs"):
            audit.audit_release(args)

    def test_firmware_resource_manifest_mismatch_fails_closed(self) -> None:
        target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "esp32_wifi_marauder"
        )
        descriptor = self._write_firmware_updater(
            targets={target: b"actual"}, manifest_data={target: b"declared"}
        )
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        with self.assertRaisesRegex(audit.AuditError, "Manifest bytes differ"):
            audit.audit_release(args)

    def test_firmware_updater_wrong_hardware_target_fails_closed(self) -> None:
        descriptor = self._write_firmware_updater(target="8")
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        with self.assertRaisesRegex(audit.AuditError, "Target differs"):
            audit.audit_release(args)

    def test_firmware_updater_release_identity_mismatch_fails_closed(self) -> None:
        descriptor = self._write_firmware_updater(
            release_tag="t-dev-004-015", firmware_version="t-dev-004-014"
        )
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        with self.assertRaisesRegex(audit.AuditError, "release/Info differ"):
            audit.audit_release(args)

    def test_stable_firmware_updater_release_identity_mismatch_fails_closed(self) -> None:
        descriptor = self._write_firmware_updater(
            release_tag="v1.0.4", firmware_version="t-flppr-fw-003"
        )
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        with self.assertRaisesRegex(audit.AuditError, "release/Info differ"):
            audit.audit_release(args)

    def test_firmware_updater_archive_limits_fail_closed(self) -> None:
        descriptor = self._write_firmware_updater()
        cases = (
            ("MAX_UPDATER_MEMBERS", 1, "too many files"),
            ("MAX_UPDATER_MEMBER_BYTES", 1, "member is too large"),
            ("MAX_UPDATER_TOTAL_BYTES", 1, "expanded size exceeds limit"),
        )
        for constant, limit, message in cases:
            with (
                self.subTest(constant=constant),
                mock.patch.object(audit, constant, limit),
                self.assertRaisesRegex(audit.AuditError, message),
            ):
                audit.load_firmware_updaters([descriptor])

    def test_firmware_updater_duplicate_normalized_path_fails_closed(self) -> None:
        descriptor = self._write_firmware_updater()
        document = json.loads(descriptor.read_text(encoding="utf-8"))
        archive_path = descriptor.parent / document["assetFileName"]
        root = "f7-update-t-dev-004-015"
        with tarfile.open(archive_path, mode="w:gz") as archive:
            for name in (f"{root}/", root):
                info = tarfile.TarInfo(name)
                info.type = tarfile.DIRTYPE
                archive.addfile(info)
        document["assetSHA256"] = audit.file_hash(archive_path, "sha256")
        descriptor.write_text(json.dumps(document), encoding="utf-8")

        with self.assertRaisesRegex(audit.AuditError, "duplicate firmware updater member"):
            audit.load_firmware_updaters([descriptor])

    def test_resource_decoder_reads_at_most_expansion_limit_plus_one(self) -> None:
        class BombReader:
            requested: Optional[int] = None

            def __init__(self, *_args, **_kwargs) -> None:
                pass

            def __enter__(self) -> "BombReader":
                return self

            def __exit__(self, *_args) -> None:
                pass

            def read(self, size: int) -> bytes:
                BombReader.requested = size
                return b"x" * size

        fake = SimpleNamespace(HeatshrinkFile=BombReader)
        header = struct.pack("<IBBB", 0x53445348, 1, 13, 6)
        with (
            mock.patch.object(audit, "MAX_RESOURCE_TOTAL_BYTES", 1024),
            mock.patch.object(audit, "_load_heatshrink2", return_value=fake),
            self.assertRaisesRegex(audit.AuditError, "expanded size exceeds limit"),
        ):
            audit._decode_resources_ths(header + b"compressed")
        self.assertEqual(BombReader.requested, 1025)

    def test_resource_manifest_unknown_record_fails_closed(self) -> None:
        with self.assertRaisesRegex(audit.AuditError, "record is invalid"):
            audit._parse_resource_manifest(b"V:0\nT:1\nX:unknown\n")

    def test_resource_header_parameters_are_exact(self) -> None:
        header = struct.pack("<IBBB", 0x53445348, 1, 15, 14)
        with self.assertRaisesRegex(audit.AuditError, "parameters differ"):
            audit._decode_resources_ths(header + b"compressed")

    def test_firmware_updater_missing_resource_target_remains_absent(self) -> None:
        unrelated_target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "quac"
        )
        descriptor = self._write_firmware_updater(
            targets={unrelated_target: b"firmware-quac"}
        )
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        result, _ = audit.audit_release(args)

        firmware_targets = {
            entry["targetPath"]
            for entry in result["entries"]
            for item in entry["targetProvenance"]
            if item["containerKind"] == "firmwareUpdaterBundle"
        }
        self.assertEqual(firmware_targets, {unrelated_target})

    def test_firmware_updater_provenance_requires_firmware_fields(self) -> None:
        target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "quac"
        )
        descriptor = self._write_firmware_updater(targets={target: b"firmware-quac"})
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]
        result, _ = audit.audit_release(args)
        item = next(
            provenance
            for entry in result["entries"]
            for provenance in entry["targetProvenance"]
            if provenance["containerKind"] == "firmwareUpdaterBundle"
        )
        item.pop("resourcesSHA256")

        with self.assertRaisesRegex(audit.AuditError, "resourcesSHA256"):
            audit.validate_audit(result)

    def test_fw_packages_provenance_rejects_firmware_only_fields(self) -> None:
        result, _ = audit.audit_release(self._args())
        item = next(
            provenance
            for entry in result["entries"]
            for provenance in entry["targetProvenance"]
            if provenance["containerKind"] == "fwPackagesZip"
        )
        item["firmwareVersion"] = "t-dev-004-015"

        with self.assertRaisesRegex(audit.AuditError, "firmware-only fields"):
            audit.validate_audit(result)

    def test_unrelated_firmware_resource_does_not_accept_missing_target(self) -> None:
        missing_target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "esp32_wifi_marauder"
        )
        unrelated_target = next(
            app["artifacts"][0]["targetPath"]
            for app in self.apps
            if app["id"] == "quac"
        )
        for path in (self.stable_manifest, self.dev_manifest):
            document = json.loads(path.read_text(encoding="utf-8"))
            document["packages"]["protected"] = [
                item
                for item in document["packages"]["protected"]
                if item["target"] != missing_target
            ]
            path.write_text(json.dumps(document), encoding="utf-8")
        for path in (self.stable_archive, self.dev_archive):
            with zipfile.ZipFile(path) as archive:
                retained = {
                    info.filename: archive.read(info)
                    for info in archive.infolist()
                    if "/ext/" + info.filename != missing_target
                }
            with zipfile.ZipFile(path, "w") as archive:
                for name, data in retained.items():
                    archive.writestr(name, data)
        descriptor = self._write_firmware_updater(
            targets={unrelated_target: b"firmware-quac"}
        )
        args = self._args()
        args.firmware_updater_descriptor = [descriptor]

        result, _ = audit.audit_release(args)

        self.assertFalse(any(item["targetPath"] == missing_target for item in result["entries"]))
        self.assertTrue(
            any(
                "esp32_wifi_marauder" in value and "firmware updater resources" in value
                for value in result["unresolved"]
            )
        )


if __name__ == "__main__":
    unittest.main()
