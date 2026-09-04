#!/usr/bin/env python3
"""Host regression tests of the real desktop loader, migrations and saved_struct I/O.

Run with a host C compiler on PATH. Optional --coverage uses Clang/LLVM to report
coverage of desktop_settings.c and enforce at least 80% line/region coverage.
Only Furi records and the storage transport are faked; fixture headers and
checksums are produced and validated by the production saved_struct code.
"""

from pathlib import Path
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "applications/services/desktop/desktop_settings.c"
FIXTURES = REPO_ROOT / "tools/tumoflip/fixtures/desktop_settings_strings"
VERSIONS = (14, 17, 18, 19, 20, 21)
COVERAGE = "--coverage" in sys.argv
if COVERAGE:
    sys.argv.remove("--coverage")


def llvm_tool(name: str) -> str:
    found = shutil.which(name)
    if found:
        return found
    return subprocess.check_output(["xcrun", "--find", name], text=True).strip()


class DesktopSettingsStringsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp_dir = tempfile.TemporaryDirectory(prefix="desktop-settings-strings-")
        cls.addClassCleanup(cls.temp_dir.cleanup)
        cls.work = Path(cls.temp_dir.name)
        cls.binary = cls.work / "settings-test"
        cls.environment = os.environ.copy()
        flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if COVERAGE else []
        if COVERAGE:
            cls.environment["LLVM_PROFILE_FILE"] = str(cls.work / "run-%p.profraw")
        command = [
            *shlex.split(os.environ.get("CC", "cc")),
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-O0",
            *flags,
            "-I", str(FIXTURES),
            "-I", str(REPO_ROOT),
            "-I", str(REPO_ROOT / "lib/toolbox"),
            str(FIXTURES / "harness.c"),
            str(REPO_ROOT / "lib/toolbox/saved_struct.c"),
            "-o", str(cls.binary),
        ]
        subprocess.run(command, check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls) -> None:
        if not COVERAGE:
            return
        profile = cls.work / "coverage.profdata"
        subprocess.run([
            llvm_tool("llvm-profdata"), "merge", "-sparse",
            *map(str, cls.work.glob("*.profraw")), "-o", str(profile),
        ], check=True, capture_output=True, text=True)
        coverage = json.loads(subprocess.check_output([
            llvm_tool("llvm-cov"), "export", str(cls.binary),
            f"-instr-profile={profile}", str(SOURCE),
        ], text=True))
        files = coverage["data"][0]["files"]
        summary = next(item["summary"] for item in files if Path(item["filename"]) == SOURCE)
        for metric in ("lines", "regions"):
            result = summary[metric]
            print(f"desktop_settings.c {metric}: {result['covered']}/{result['count']} "
                  f"({result['percent']:.2f}%)")
            if result["percent"] < 80:
                raise AssertionError(f"desktop_settings.c {metric} coverage is below 80%")

    def run_case(self, mode: str, versions: tuple[int, ...] = VERSIONS) -> None:
        for version in versions:
            with self.subTest(mode=mode, version=version):
                result = subprocess.run(
                    [str(self.binary), mode, str(version)],
                    env=self.environment, text=True, capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f"PASS: {mode} v{version}", result.stdout)

    def test_full_width_strings_are_terminated_without_losing_prefixes(self) -> None:
        self.run_case("malformed")

    def test_migrations_save_only_already_sanitized_strings(self) -> None:
        self.run_case("migration-saved", VERSIONS[:-1])

    def test_valid_settings_and_all_shortcuts_are_preserved(self) -> None:
        self.run_case("valid")

    def test_invalid_payloads_default_for_every_supported_version(self) -> None:
        for mode in ("short-payload", "bad-magic", "bad-checksum"):
            self.run_case(mode)

    def test_missing_metadata_and_unknown_versions_default(self) -> None:
        for mode in ("missing", "short-header", "unsupported-version"):
            self.run_case(mode, (21,))

    def test_failed_migration_saves_still_return_safe_settings(self) -> None:
        for mode in ("save-failure", "short-write"):
            self.run_case(mode, VERSIONS[:-1])

    def test_default_settings_survive_a_save_failure(self) -> None:
        self.run_case("defaults-save-failure", (21,))


if __name__ == "__main__":
    unittest.main()
