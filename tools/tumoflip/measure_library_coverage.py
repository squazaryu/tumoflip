"""Measure native library/sensor/storage coverage; GUI and hardware are excluded."""
import argparse
import ast
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "applications/system/device_library"
FIXTURES = ROOT / "tools/tumoflip/fixtures"


def fixture(method):
    tree = ast.parse((ROOT / "tools/tumoflip/test_device_library_sensor.py").read_text())
    node = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == method)
    call = next(n for n in ast.walk(node) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute) and n.func.attr == "native")
    return ast.literal_eval(call.args[0])


def io_program(directory):
    parts = ['#include "library_io_stubs.h"\n']
    locations = {}
    for path in (LIBRARY / "library_model.h", ROOT / "lib/toolbox/file_history.h",
                 LIBRARY / "library_model.c", LIBRARY / "card_store.h",
                 LIBRARY / "card_store.c", LIBRARY / "history_engine.c"):
        mapped = directory / ("host_" + path.name)
        mapped.write_text(re.sub(r"^#(?:include|pragma).*\n", "\n", path.read_text(), flags=re.M))
        locations[path] = mapped
        parts.append(f'#include "{mapped}"\n')
    parts.append((FIXTURES / "library_io_host.c").read_text())
    return "\n".join(parts), locations


def measure(output):
    report = {"scope": "native models and storage contracts only; no GUI/RTOS/hardware coverage", "files": {}}
    cases = [
        (fixture("test_library_bounds_and_retention"), [LIBRARY / "library_model.c"], []),
        (fixture("test_sensor_training_and_held_out_validation"), [ROOT / "applications_user/signal_workbench/sensor_fit.c"], []),
        (None, [], [LIBRARY / "card_store.c", LIBRARY / "history_engine.c"]),
    ]
    for program, sources, mapped in cases:
        with tempfile.TemporaryDirectory(prefix="library-coverage-") as directory:
            tmp = Path(directory); (tmp / "sd").mkdir()
            src, binary, raw, profile = tmp / "test.c", tmp / "test", tmp / "test.profraw", tmp / "test.profdata"
            locations = {}
            if mapped: program, locations = io_program(tmp)
            src.write_text(program)
            extra = []
            if mapped:
                extra = [ROOT / "lib/mbedtls/library/sha256.c", ROOT / "lib/mbedtls/library/platform_util.c"]
            command = ["clang", "-std=c11", "-D_DEFAULT_SOURCE", "-O0", "-g", "-fprofile-instr-generate", "-fcoverage-mapping",
                '-DMBEDTLS_CONFIG_FILE="library_crypto_config.h"', "-I", str(FIXTURES), "-I", str(ROOT / "lib/mbedtls/include")]
            if sources: command += ["-I", str(sources[0].parent)]
            subprocess.run(command + [str(src), *map(str, sources + extra), "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(tmp / "sd")], check=True, env={**os.environ, "LLVM_PROFILE_FILE": str(raw)})
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", str(raw), "-o", str(profile)], check=True)
            coverage = json.loads(subprocess.check_output(["xcrun", "llvm-cov", "export", str(binary), f"-instr-profile={profile}"], text=True))
            for path in sources + mapped:
                item = next(item for item in coverage["data"][0]["files"] if Path(item["filename"]).resolve() == locations.get(path, path).resolve())
                report["files"][str(path.relative_to(ROOT))] = item["summary"]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return all(item[metric]["percent"] >= 80 for item in report["files"].values() for metric in ("lines", "branches", "functions"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("output", type=Path)
    raise SystemExit(0 if measure(parser.parse_args().output) else 1)
