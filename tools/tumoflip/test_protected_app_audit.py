import hashlib
import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from typing import Optional

from tools.tumoflip import protected_app_audit as audit


REPO_ROOT = Path(__file__).resolve().parents[2]
REGISTRY_PATH = REPO_ROOT / "tools/tumoflip/protected_apps_registry.json"
RAW_EDIT_HEAD = "d60ee2a34fa87b89c99f1ba9056737765f9f921f"


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
            generated_at="2026-08-12T00:00:00+00:00",
        )

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

    def test_changed_raw_edit_is_explicitly_unresolved_and_omitted(self) -> None:
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

    def test_same_pack_is_reaudited_when_target_release_changes(self) -> None:
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


if __name__ == "__main__":
    unittest.main()
