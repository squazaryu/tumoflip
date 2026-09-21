#!/usr/bin/env python3
"""ASan/UBSan and LLVM region coverage of production functions in the host fixtures.

This is NOT whole-firmware or hardware coverage. Storage, RTOS and radio transports
remain test doubles. Only explicitly named extracted production functions count.
Run as: python -m tools.tumoflip.measure_upstream_1435_tests
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tumoflip import test_upstream_1435_native as adaptation
from tools.tumoflip import test_ibutton_write_targets_native as ibutton

FUNCTIONS = {
    "hid_mouse_jiggler_timer_callback", "hid_mouse_jiggler_exit_callback",
    "hid_mouse_jiggler_input_callback", "hid_mouse_jiggler_stealth_random_move",
    "hid_mouse_jiggler_free", "hid_mouse_jiggler_stealth_free",
    "hid_mouse_jiggler_stealth_random_period", "hid_mouse_jiggler_stealth_timer_callback",
    "hid_mouse_jiggler_stealth_exit_callback", "hid_mouse_jiggler_stealth_input_callback",
    "subghz_txrx_gen_secplus_v2_protocol", "number_input_set_result_callback",
    "number_input_get_value", "is_number_too_large", "is_number_too_small",
    "number_input_handle_ok", "ibutton_protocol_group_dallas_write_id",
    "ibutton_settings_get_write_targets", "ibutton_worker_write_report",
    "ibutton_worker_write_set_target", "ibutton_worker_mode_write_id_tick",
    "ibutton_worker_mode_write_copy_tick",
    "ibutton_settings_write_targets_on_save",
}


def llvm_tool(name):
    return shutil.which(name) or subprocess.check_output(
        ["xcrun", "--find", name], text=True).strip()


def main():
    coverage = []

    def run_instrumented(body):
        with tempfile.TemporaryDirectory(prefix="tumoflip-1435-coverage-") as directory:
            root = Path(directory)
            src, exe = root / "fixture.c", root / "fixture"
            src.write_text(body)
            subprocess.run([
                "clang", "-std=c11", "-Wall", "-Werror", "-g", "-O0",
                "-fsanitize=address,undefined", "-fprofile-instr-generate", "-fcoverage-mapping",
                str(src), "-o", str(exe),
            ], check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True, text=True,
                           env={**os.environ, "LLVM_PROFILE_FILE": str(root / "run.profraw")})
            profile = root / "run.profdata"
            subprocess.run([llvm_tool("llvm-profdata"), "merge", "-sparse",
                            str(root / "run.profraw"), "-o", str(profile)],
                           check=True, capture_output=True, text=True)
            report = json.loads(subprocess.check_output([
                llvm_tool("llvm-cov"), "export", str(exe), f"-instr-profile={profile}",
            ], text=True))
            for item in report["data"][0]["functions"]:
                name = item["name"].rsplit(":", 1)[-1]
                if name in FUNCTIONS:
                    # File 0 is the extracted function body. Other file IDs are macro
                    # expansions (including the fixture's mocked locks/assertions), not
                    # production logic; counting their abort branches distorts coverage.
                    regions = [region for region in item["regions"]
                               if region[7] == 0 and region[5] == 0]
                    coverage.append({"function": name, "regions": len(regions),
                                     "covered": sum(region[4] > 0 for region in regions),
                                     "uncovered": [body.splitlines()[region[0]-1].strip()
                                                   for region in regions if not region[4]]})

    adaptation.run_c = ibutton.run_c = run_instrumented
    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromModule(module)
                               for module in (adaptation, ibutton))
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    total = sum(item["regions"] for item in coverage)
    covered = sum(item["covered"] for item in coverage)
    missing = sorted(FUNCTIONS - {item["function"] for item in coverage})
    percent = 100 * covered / total if total else 0
    print(json.dumps({"scope": "extracted production C functions, excluding mocked macro expansions",
                      "regions": total, "covered": covered, "percent": round(percent, 2),
                      "missing_functions": missing, "cases": coverage}, indent=2))
    return 0 if result.wasSuccessful() and not missing and percent >= 80 else 1


if __name__ == "__main__":
    raise SystemExit(main())
