"""Measure native parser line coverage using the existing synthetic test fixtures.

Requires clang and xcrun llvm-profdata/llvm-cov. This does not measure GUI, RTOS,
radio or hardware coverage. It never executes code extracted from capture files.
"""
import argparse
import ast
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = (
    ("test_capture_inspector.py", "program", "applications/system/capture_inspector/capture_model.c"),
    ("test_arf_elf_metadata.py", "fixture", "applications_user/arf_tools/arf_elf_metadata.c"),
)


def literal_fixture(path, name):
    for node in ast.walk(ast.parse(path.read_text())):
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == name for target in node.targets
        ):
            return ast.literal_eval(node.value)
    raise ValueError(f"Missing literal fixture {name}: {path}")


def measure(output):
    report = {"scope": "native capture and ELF parsers only; not GUI/hardware", "files": {}}
    for test, name, relative in CASES:
        source = ROOT / relative
        with tempfile.TemporaryDirectory(prefix="inspector-coverage-") as directory:
            work = Path(directory)
            fixture = work / "fixture.c"
            fixture.write_text(literal_fixture(ROOT / "tools/tumoflip" / test, name))
            binary, raw, profile = work / "test", work / "test.profraw", work / "test.profdata"
            subprocess.run(["clang", "-std=c11", "-O0", "-g", "-fprofile-instr-generate",
                            "-fcoverage-mapping", "-I", str(source.parent), str(fixture),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, env={**os.environ, "LLVM_PROFILE_FILE": str(raw)})
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", str(raw), "-o", str(profile)], check=True)
            coverage = json.loads(subprocess.check_output(
                ["xcrun", "llvm-cov", "export", str(binary), f"-instr-profile={profile}"], text=True))
            item = next(item for item in coverage["data"][0]["files"] if Path(item["filename"]) == source)
            report["files"][relative] = item["summary"]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return all(item[metric]["percent"] >= 80
               for item in report["files"].values() for metric in ("lines", "branches", "functions"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    raise SystemExit(0 if measure(parser.parse_args().output) else 1)
