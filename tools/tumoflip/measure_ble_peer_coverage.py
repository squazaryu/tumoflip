"""Measure production peer-policy/store/scene coverage from native host fixtures."""
import argparse
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

CASES = [
    ("test_ble_remote_peers", "BleRemotePeersTests", "test_controller_peer_policy_is_bounded_and_fail_closed",
     ["targets/f7/ble_glue/gap_peer_policy.c"]),
    ("test_hid_peer_store", "HidPeerStoreTests", "test_preferences_are_journalled_and_validated",
     ["applications/system/hid_app/helpers/hid_peer_store.c"]),
    ("test_hid_peer_scenes", "HidPeerScenesTests", "test_select_pair_cancel_forget_name_and_restart",
     ["applications/system/hid_app/scenes/hid_scene_devices.c", "applications/system/hid_app/scenes/hid_scene_peer_name.c"]),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for module_name, class_name, method, paths in CASES:
        directory = args.output.resolve() / module_name
        directory.mkdir(exist_ok=True)

        def instrument(body):
            mapped_paths = {}
            for path in paths:
                lines = (ROOT / path).read_text().splitlines()
                selected = [line for line in lines if not line.startswith(("#include", "#pragma"))]
                source = "\n".join(selected)
                if source not in body:
                    raise AssertionError(f"production body not found: {path}")
                # Separate physical include files keep production coverage apart
                # from the host adapter (LLVM coverage ignores #line filenames).
                mapped = "\n".join("" if line.startswith(("#include", "#pragma")) else line for line in lines)
                production = directory / Path(path).name
                production.write_text(mapped + "\n")
                mapped_paths[str(production)] = path
                body = body.replace(source, f'\n#include "{production}"\n')
            source_path = directory / "fixture.c"
            source_path.write_text(body)
            executable = directory / "fixture"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-g", "-fprofile-instr-generate",
                            "-fcoverage-mapping", str(source_path), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True,
                           env={**os.environ, "LLVM_PROFILE_FILE": str(directory / "run.profraw")})
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", str(directory / "run.profraw"),
                            "-o", str(directory / "run.profdata")], check=True)
            report = json.loads(subprocess.check_output([
                "xcrun", "llvm-cov", "export", str(executable),
                f"-instr-profile={directory / 'run.profdata'}"], text=True))
            for item in report["data"][0]["files"]:
                if item["filename"] in mapped_paths:
                    results.append({"file": mapped_paths[item["filename"]], **item["summary"]})

        module = importlib.import_module("tools.tumoflip." + module_name)
        result = unittest.TestResult()
        with patch.object(module, "run_c", instrument):
            getattr(module, class_name)(method).run(result)
        if not result.wasSuccessful():
            raise AssertionError(result.errors + result.failures)
    if len(results) != sum(len(case[3]) for case in CASES):
        raise AssertionError("missing production coverage")
    (args.output / "coverage.json").write_text(json.dumps(results, indent=2) + "\n")
    for item in results:
        print(f"{item['file']}: lines={item['lines']['percent']:.1f}% functions={item['functions']['percent']:.1f}%")
    if any(item["lines"]["percent"] < 80 for item in results):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
