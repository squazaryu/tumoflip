#!/usr/bin/env python3
"""Host verification for Quac's clean-room Picopass playback boundary."""

from __future__ import annotations

import os
from pathlib import Path
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
QUAC = ROOT / "applications_user/quac"
PICOPASS = QUAC / "actions/picopass"
FIXTURE = ROOT / "tools/tumoflip/fixtures/quac_picopass/runner.c"
SANITIZE = "--sanitize" in sys.argv
if SANITIZE:
    sys.argv.remove("--sanitize")
COVERAGE = "--coverage" in sys.argv
if COVERAGE:
    sys.argv.remove("--coverage")


def llvm_tool(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found
    return subprocess.check_output(["xcrun", "--find", name], text=True).strip()


class QuacPicopassHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp = tempfile.TemporaryDirectory(prefix="quac-picopass-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "quac_picopass_test"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        command = [
            *compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wconversion",
            "-Wshadow",
            "-DQUAC_PICOPASS_HOST_TEST",
            f"-I{PICOPASS}",
            str(FIXTURE),
            str(PICOPASS / "quac_picopass.c"),
            "-o",
            str(cls.binary),
        ]
        if SANITIZE:
            command.extend(
                (
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-fno-sanitize-recover=all",
                )
            )
        if COVERAGE:
            command.extend(("-fprofile-instr-generate", "-fcoverage-mapping"))
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls) -> None:
        if not COVERAGE:
            return
        profile = Path(cls.temp.name) / "coverage.profdata"
        raw_profiles = sorted(Path(cls.temp.name).glob("coverage-*.profraw"))
        if not raw_profiles:
            raise AssertionError("coverage run produced no profiles")
        subprocess.run(
            [
                llvm_tool("llvm-profdata"),
                "merge",
                "-sparse",
                *map(str, raw_profiles),
                "-o",
                str(profile),
            ],
            check=True,
            capture_output=True,
        )
        sources = [PICOPASS / "quac_picopass.c"]
        report = json.loads(
            subprocess.check_output(
                [
                    llvm_tool("llvm-cov"),
                    "export",
                    str(cls.binary),
                    f"-instr-profile={profile}",
                    *map(str, sources),
                ],
                text=True,
            )
        )
        summaries = {
            Path(item["filename"]).resolve(): item["summary"]
            for item in report["data"][0]["files"]
        }
        for source in sources:
            summary = summaries[source.resolve()]
            percentages = {
                kind: round(summary[kind]["percent"], 2)
                for kind in ("lines", "regions")
            }
            print(source.name, "production coverage:", percentages)
            if any(percentages[kind] < 80 for kind in percentages):
                raise AssertionError(f"{source.name} production coverage is below 80%")

    def run_case(self, case: str) -> None:
        environment = os.environ.copy()
        if SANITIZE:
            leak_check = "0" if sys.platform == "darwin" else "1"
            environment["ASAN_OPTIONS"] = f"detect_leaks={leak_check}:halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1"
        if COVERAGE:
            environment["LLVM_PROFILE_FILE"] = str(
                Path(self.temp.name) / "coverage-%p.profraw"
            )
        result = subprocess.run(
            [str(self.binary), case],
            capture_output=True,
            text=True,
            timeout=5,
            env=environment,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_parser_accepts_exact_saved_credential(self) -> None:
        self.run_case("parse-valid")

    def test_parser_rejects_malformed_or_ambiguous_files(self) -> None:
        for case in (
            "bad-header",
            "bad-version",
            "truncated",
            "duplicate-block",
            "unknown-field",
            "invalid-hex",
            "extra-hex",
            "missing-block",
            "small-limit",
            "large-limit",
            "block-over-limit",
            "embedded-nul",
            "oversized-line",
        ):
            with self.subTest(case=case):
                self.run_case(case)

    def test_duration_parser_is_strict_and_bounded(self) -> None:
        self.run_case("duration")

    def test_activation_selection_and_public_reads(self) -> None:
        self.run_case("activation")

    def test_secure_credentials_are_rejected_before_listener_start(self) -> None:
        for case in (
            "secure-fuses",
            "encryption-unknown",
            "encryption-unrecognized",
            "encryption-des",
            "encryption-3des",
            "key-material",
            "lifecycle-secure",
        ):
            with self.subTest(case=case):
                self.run_case(case)

    def test_nonsecure_reads_are_bounded(self) -> None:
        self.run_case("read-policy")
        self.run_case("read4")

    def test_halt_reselect_and_unknown_frames_are_finite(self) -> None:
        self.run_case("halt-reselect")
        self.run_case("unknown")

    def test_runtime_cleanup_is_exact_on_success_cancel_and_failure(self) -> None:
        for case in (
            "lifecycle-success",
            "lifecycle-cancel",
            "lifecycle-load-fail",
            "lifecycle-start-fail",
            "lifecycle-wait-fail",
        ):
            with self.subTest(case=case):
                self.run_case(case)

    def test_sensitive_structures_are_zeroed(self) -> None:
        self.run_case("zeroize")


class QuacPicopassIntegrationGuardTest(unittest.TestCase):
    def test_direct_playlist_and_existing_routes_remain_wired(self) -> None:
        action = (QUAC / "actions/action.c").read_text()
        adapter = (QUAC / "actions/action_picopass.c").read_text()
        playlist = (QUAC / "actions/action_qpl.c").read_text()
        declarations = (QUAC / "actions/action_i.h").read_text()
        self.assertIn('!strcmp(item->ext, ".picopass")', action)
        self.assertIn("action_picopass_tx(context, path, error)", action)
        self.assertIn("action_picopass_tx", declarations)
        self.assertIn('!strcmp(ext, ".picopass")', playlist)
        self.assertIn("picopass_duration", playlist)
        self.assertIn('!strcmp(item->ext, ".qab")', action)
        self.assertIn("action_appbridge_tx(context, path, error)", action)
        self.assertRegex(adapter, r"#define\s+QUAC_PICOPASS_FDT_FC\s+4475U")
        self.assertIn("Picopass: Unsupported secure card", adapter)

    def test_item_and_import_use_the_existing_quac_nfc_icon(self) -> None:
        item_h = (QUAC / "item.h").read_text()
        item_c = (QUAC / "item.c").read_text()
        scene_items = (QUAC / "scenes/scene_items.c").read_text()
        scene_actions = (QUAC / "scenes/scene_action_settings.c").read_text()
        menu = (QUAC / "views/action_menu.c").read_text()
        self.assertIn("Item_Picopass", item_h)
        self.assertIn('!strcmp(ext, ".picopass")', item_c)
        self.assertIn("ActionMenuItemTypePicopass", scene_items)
        self.assertIn("ActionMenuItemTypePicopass", menu)
        self.assertIn("&I_NFC_10px", menu)
        self.assertIn('!strcmp(ext, ".picopass")', scene_actions)
        self.assertIn("&I_NFC_10px", scene_actions)
        self.assertFalse((QUAC / "images/Picopass_10px.png").exists())

    def test_settings_schema_migrates_v1_and_bounds_picopass_duration(self) -> None:
        settings = (QUAC / "quac_settings.c").read_text()
        header = (QUAC / "quac.h").read_text()
        scene = (QUAC / "scenes/scene_settings.c").read_text()
        self.assertRegex(settings, r"#define\s+QUAC_SETTINGS_FILE_VERSION\s+2")
        self.assertRegex(settings, r"#define\s+QUAC_SETTINGS_FILE_VERSION_LEGACY\s+1")
        self.assertIn('"Picopass Duration"', settings)
        self.assertIn("picopass_duration", header)
        self.assertIn('"Picopass Duration"', scene)

    def test_package_ownership_broker_and_ci_contracts_are_preserved(self) -> None:
        manifest = (QUAC / "application.fam").read_text()
        self.assertIn('appid="quac"', manifest)
        self.assertIn('requires=["subghz_radio_broker"]', manifest)
        self.assertIn("fap_package_only=True", manifest)
        self.assertIn('fap_version="0.10.0"', manifest)
        package_test = (ROOT / "tools/tumoflip/test_quac_package_migration.py").read_text()
        self.assertIn('QUAC_DATA = "/ext/apps_data/quac"', package_test)
        for workflow in ("pr-build.yml", "release.yml"):
            contents = (ROOT / ".github/workflows" / workflow).read_text()
            self.assertIn("tools/tumoflip/test_quac_picopass.py", contents)
        self.assertIn("fap_quac", (ROOT / ".github/workflows/pr-build.yml").read_text())

    def test_provenance_is_exact_and_forbidden_sources_are_absent(self) -> None:
        notice = (PICOPASS / "PROVENANCE.md").read_text()
        self.assertIn("08bafc478e98f6d179e059759cc13d9bf199a151", notice)
        self.assertIn("47d0a8fa00ee53d3f93c9fe00fb1a44b8180e3d3", notice)
        self.assertIn("GPL-3.0-or-later", notice)
        text = "\n".join(
            path.read_text(errors="replace")
            for path in PICOPASS.rglob("*")
            if path.is_file() and path.suffix in (".c", ".h", ".md")
        ).lower()
        self.assertNotIn("bettse", text)
        self.assertNotIn("5f3f1ec", text)
        source = "\n".join(
            path.read_text(errors="replace")
            for path in PICOPASS.rglob("*")
            if path.is_file() and path.suffix in (".c", ".h")
        ).lower()
        self.assertNotIn("hash1(", source)
        self.assertNotIn("hash2(", source)
        self.assertNotIn("diversify", source)
        self.assertNotIn("loclass_elite", source)
        self.assertNotIn("elite_kdf", source)
        self.assertNotIn("bool elite", source)
        self.assertNotIn("reader_mac", source)
        self.assertNotIn("tag_mac", source)
        self.assertNotIn("authenticated", source)
        self.assertFalse((PICOPASS / "quac_picopass_mac.c").exists())
        self.assertFalse((PICOPASS / "quac_picopass_mac.h").exists())


if __name__ == "__main__":
    unittest.main()
