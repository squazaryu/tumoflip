#!/usr/bin/env python3
"""Host-execute Quac's production parser/action boundaries with fault injection."""

from pathlib import Path
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

try:
    from .test_main_menu_layouts import function_definition
except ImportError:
    from test_main_menu_layouts import function_definition

ROOT = Path(__file__).resolve().parents[2]
QUAC = ROOT / "applications_user/quac"
FIXTURES = ROOT / "tools/tumoflip/fixtures/quac_lifecycle"
COVERAGE = "--coverage" in sys.argv
if COVERAGE:
    sys.argv.remove("--coverage")


def llvm_tool(name):
    return shutil.which(name) or subprocess.check_output(["xcrun", "--find", name], text=True).strip()


def functions(path, names):
    source = path.read_text()
    result = []
    for name in names:
        definition = function_definition(source, name)
        line = source[:source.index(definition)].count("\n") + 1
        result.append(f'#line {line} "{path}"\n{definition}\n')
    return "\n".join(result)


class QuacLifecycleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="quac-lifecycle-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.binaries = {}
        cls.sources = {}
        targets = {
            "ir": ("actions/action_ir_utils.c", (
                "infrared_utils_signal_alloc", "infrared_utils_signal_free",
                "infrared_utils_read_signal_at_index", "infrared_utils_write_signal",
            )),
            "subghz": ("actions/action_subghz.c", (
                "action_subghz_raw_end_callback", "action_subghz_tx",
            )),
            "start": ("actions/helpers/subghz_txrx.c", ("subghz_txrx_tx_start",)),
        }
        for target, (relative, names) in targets.items():
            path = QUAC / relative
            source = path.read_text()
            # Include any newly factored local ownership helper without copying it.
            if target == "ir":
                helper = "infrared_utils_signal_reset"
                if re.search(rf"static void {helper}\(", source):
                    names = (helper, *names)
            template = (FIXTURES / f"{target}.c").read_text()
            if target == "ir":
                header = (QUAC / "actions/action_ir_utils.h").read_text()
                header = re.sub(r"^#include.*$", "", header, flags=re.MULTILINE)
                template = template.replace("@SIGNAL_HEADER@", header)
            production = cls.directory / f"{target}_production.inc"
            production.write_text(functions(path, names))
            combined = template.replace("@PRODUCTION@", f'#include "{production}"')
            generated = cls.directory / f"{target}.c"
            generated.write_text(combined)
            binary = cls.directory / target
            command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-g",
                       "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                       "-Wno-unused-function", str(generated), "-o", str(binary)]
            if COVERAGE:
                command.extend(("-fprofile-instr-generate", "-fcoverage-mapping"))
            built = subprocess.run(command, capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stdout + built.stderr)
            cls.binaries[target] = binary
            cls.sources[target] = production

    @classmethod
    def tearDownClass(cls):
        if not COVERAGE:
            return
        for target, binary in cls.binaries.items():
            profile = cls.directory / f"{target}.profdata"
            subprocess.run([llvm_tool("llvm-profdata"), "merge", "-sparse",
                            *map(str, cls.directory.glob(f"{target}-*.profraw")),
                            "-o", str(profile)], check=True, capture_output=True)
            data = json.loads(subprocess.check_output([
                llvm_tool("llvm-cov"), "export", str(binary), f"-instr-profile={profile}",
                str(cls.sources[target]),
            ], text=True))
            summary = next(file["summary"] for file in data["data"][0]["files"]
                           if Path(file["filename"]) == cls.sources[target])
            print(target, "production coverage:", {
                kind: round(summary[kind]["percent"], 2) for kind in ("lines", "regions")
            })
            if any(summary[kind]["percent"] < 80 for kind in ("lines", "regions")):
                raise AssertionError(f"{target} production coverage is below 80%")

    def run_cases(self, target, cases):
        for case in cases:
            with self.subTest(target=target, case=case):
                environment = os.environ.copy()
                if COVERAGE:
                    environment["LLVM_PROFILE_FILE"] = str(self.directory / f"{target}-%p.profraw")
                result = subprocess.run([str(self.binaries[target]), case], env=environment,
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ir_failure_cleanup(self):
        self.run_cases("ir", ("data-failure", "early-raw-failure", "invalid-count", "empty-raw",
                              "read-header", "read-type", "read-duty_cycle", "read-count",
                              "read-protocol", "read-address", "invalid-protocol", "unknown-type",
                              "bad-version", "bad-type", "missing-index"))

    def test_ir_reused_import_signal(self):
        self.run_cases("ir", ("raw-raw", "raw-parsed", "parsed-raw-failure", "raw-failure"))

    def test_ir_legitimate_read_write_and_temp_strings(self):
        self.run_cases("ir", ("raw", "parsed", "missing-command", "parsed-failure"))

    def test_ir_write_failures_preserve_owned_payload(self):
        self.run_cases("ir", tuple("write-" + key for key in (
            "header", "comment", "name", "type", "frequency", "duty_cycle",
            "data", "protocol", "address", "command",
        )))

    def test_subghz_parse_failures_never_start_transmission(self):
        self.run_cases("subghz", ("open", "header", "version", "frequency", "preset",
                                  "custom", "protocol", "decoder", "deserialize", "copy"))

    def test_subghz_start_failures_never_wait_or_access_encoder(self):
        self.run_cases("subghz", ("start-raw", "start-parsed"))

    def test_subghz_legitimate_transmission(self):
        self.run_cases("subghz", ("raw", "parsed", "default-frequency", "valid-custom"))

    def test_backend_honors_async_start_failure(self):
        self.run_cases("start", ("async-failure", "only-rx", "deserialize", "no-encoder"))

    def test_backend_legitimate_start(self):
        self.run_cases("start", ("success",))

    def test_backend_parser_failures(self):
        self.run_cases("start", ("rewind", "protocol", "repeat", "preset", "frequency"))

    def test_safety_patch_identity_and_ci_gates(self):
        self.assertIn('fap_version="0.9.2"', (QUAC / "application.fam").read_text())
        for workflow in ("pr-build.yml", "release.yml"):
            text = (ROOT / ".github/workflows" / workflow).read_text()
            self.assertTrue("tools/tumoflip/test_quac_lifecycle.py" in text,
                            f"{workflow} must run the Quac lifecycle regression")
        workflow = (ROOT / ".github/workflows/pr-build.yml").read_text()
        self.assertTrue("fap_quac" in workflow, "PR CI must APPCHK the owning FAP")


if __name__ == "__main__":
    unittest.main()
