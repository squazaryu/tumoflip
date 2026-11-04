"""Exercise the actual diagnostic runner; no SD card or device is opened."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_upstream_20261104 import ROOT, body


class DiagnosticRunnerReportingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="unit-report-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        source = (ROOT / "applications/debug/unit_tests/test_runner.c").read_text()
        structure = re.search(r"struct TestRunner \{.*?\n\};", source, re.S).group()
        names = ["test_runner_run_plugin", "test_runner_prepare", "test_runner_run_internal",
                 "test_runner_print_report", "test_runner_run"]
        production = "\n".join(body(source, name) for name in names
                               if re.search(r"\b" + name + r"\(", source))
        fixture = (ROOT / "tools/tumoflip/fixtures/upstream_20261104/test_runner_host.c").read_text()
        cls.input = cls.work / "test.c"
        cls.input.write_text(fixture.replace("/* STRUCT */", structure).replace("/* PRODUCTION */", production))
        cls.binary = cls.work / "test"
        cls.coverage = os.environ.get("UNIT_REPORT_COVERAGE") == "1"
        flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if cls.coverage else []
        result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                                 "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *flags,
                                 str(cls.input), "-o", str(cls.binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def test_each_result_is_truthful_and_all_resources_are_released(self):
        for scenario in ("empty", "missing", "aggregate", "foreign", "directory", "mkdir1", "mkdir2",
                         "interrupt", "locked", "pass", "gain", "leak"):
            with self.subTest(scenario=scenario):
                result = subprocess.run([str(self.binary), scenario], capture_output=True, text=True,
                                        env={**os.environ, "LLVM_PROFILE_FILE": str(self.work / "%p.profraw")})
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
