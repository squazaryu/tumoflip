"""Execute actual Standard hopping/catalog code; all radio and file I/O is fake."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]


class HopperProfileFilterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="tumo-hopper-profile-")
        cls.work = Path(cls.temporary.name)
        source = (ROOT / "applications/main/subghz/helpers/subghz_txrx.c").read_text()
        profiles = (ROOT / "lib/subghz/subghz_rx_profiles.h").read_text()
        implementation = ROOT / "lib/subghz/subghz_rx_profiles.c"
        if implementation.exists():
            profiles += "\n" + implementation.read_text()
        plan = (ROOT / "lib/subghz/subghz_hopper_plan.h").read_text()
        actual = "\n".join(line for line in (profiles + "\n" + plan).splitlines()
                           if not line.startswith(("#include", "#pragma once")))
        actual += "\n" + "\n".join(re.findall(r"^#define SUBGHZ_HOPPER_.*$", source, re.M))
        helper = "static int subghz_txrx_hopper_profile_preset_index("
        if helper in source:
            actual += "\n" + function(source, helper)
        actual += "\n" + function(source, "void subghz_txrx_hopper_update(")
        initialization = re.search(
            r"(?:instance->rx_profile_preset_owned\s*=\s*)?subghz_rx_profiles_init\(instance->setting\);",
            function(source, "SubGhzTxRx* subghz_txrx_alloc("),
        ).group()
        fixture = (ROOT / "tools/tumoflip/tests/hopper_profile_host.c").read_text()
        cls.input = cls.work / "test.c"
        cls.input.write_text(fixture.replace("/* ACTUAL_PRODUCTION */", actual)
                            .replace("/* ACTUAL_PROFILE_INIT */", initialization))
        cls.binary = cls.work / "test"
        cls.coverage = os.environ.get("HOPPER_HOST_COVERAGE") == "1"
        flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if cls.coverage else []
        result = subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *flags,
            str(cls.input), "-o", str(cls.binary),
        ], capture_output=True, text=True)
        if result.returncode:
            cls.temporary.cleanup()
            raise AssertionError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        try:
            if cls.coverage:
                profiles = sorted(str(p) for p in cls.work.glob("*.profraw"))
                merged = cls.work / "coverage.profdata"
                subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", *profiles,
                                "-o", str(merged)], check=True)
                subprocess.run(["xcrun", "llvm-cov", "report", str(cls.binary),
                                "-instr-profile=" + str(merged), "-show-functions",
                                "-name-regex=subghz_txrx_hopper_update|init_profiles",
                                str(cls.input)], check=True)
                # Export is printed only when explicitly requested for a coverage receipt.
                report = json.loads(subprocess.check_output([
                    "xcrun", "llvm-cov", "export", str(cls.binary),
                    "-instr-profile=" + str(merged),
                ], text=True))
                for item in report["data"][0]["functions"]:
                    if item["name"].endswith(("subghz_txrx_hopper_update", "init_profiles")):
                        regions = [r for r in item["regions"] if r[-1] == 0]
                        covered = sum(r[4] > 0 for r in regions)
                        if not regions or covered / len(regions) < 0.8:
                            raise AssertionError("New hopping-filter region coverage below 80%")
        finally:
            cls.temporary.cleanup()

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True,
                                env={**os.environ, "LLVM_PROFILE_FILE": str(self.work / "%p.profraw")})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_automatic_preset_and_combined_skip_owned_honda_in_every_position(self):
        self.run_case("automatic")

    def test_explicit_user_hopping_list_preserves_order_and_duplicates(self):
        self.run_case("configured")

    def test_tx_power_changes_do_not_reintroduce_the_builtin_rx_profile(self):
        self.run_case("power")

    def test_custom_presets_and_user_owned_honda_name_are_not_removed(self):
        self.run_case("custom")

    def test_user_honda_collision_with_only_pa_difference_is_preserved(self):
        self.run_case("custom-power")

    def test_frequency_only_keeps_the_explicitly_selected_preset(self):
        self.run_case("frequency")

    def test_honda_rx_selection_survives_before_and_after_automatic_scan(self):
        self.run_case("profile")

    def test_empty_lists_stale_indices_hold_pause_and_radio_state(self):
        self.run_case("boundaries")


if __name__ == "__main__":
    unittest.main()
