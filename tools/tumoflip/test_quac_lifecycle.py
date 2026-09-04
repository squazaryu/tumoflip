#!/usr/bin/env python3
"""Host-execute Quac's production parser/action boundaries with fault injection."""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest

try:
    from .test_main_menu_layouts import function_definition
except ImportError:
    from test_main_menu_layouts import function_definition

ROOT = Path(__file__).resolve().parents[2]
QUAC = ROOT / "applications_user/quac"
FIXTURES = ROOT / "tools/tumoflip/fixtures/quac_lifecycle"


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
            combined = template.replace("@PRODUCTION@", functions(path, names))
            generated = cls.directory / f"{target}.c"
            generated.write_text(combined)
            binary = cls.directory / target
            command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-g",
                       "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                       "-Wno-unused-function", str(generated), "-o", str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stdout + built.stderr)
            cls.binaries[target] = binary

    def run_cases(self, target, cases):
        for case in cases:
            with self.subTest(target=target, case=case):
                result = subprocess.run([str(self.binaries[target]), case],
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ir_failure_cleanup(self):
        self.run_cases("ir", ("data-failure", "early-raw-failure", "invalid-count"))

    def test_ir_reused_import_signal(self):
        self.run_cases("ir", ("raw-raw", "raw-parsed", "parsed-raw-failure", "raw-failure"))

    def test_ir_legitimate_read_write_and_temp_strings(self):
        self.run_cases("ir", ("raw", "parsed", "missing-command", "parsed-failure"))

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


if __name__ == "__main__":
    unittest.main()
