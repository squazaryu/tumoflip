"""Coverage of production profile validation and streaming RAW parser (not GUI/hardware)."""
import argparse
import ast
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = (
    ("test_profile_validation", "applications/main/subghz/plugins/workspaces/workspace_model.c"),
    ("test_streaming_raw_parser", "applications_user/tumo_acceptance_suite/corpus_raw.c"),
)


def measure(output):
    tree = ast.parse((ROOT / "tools/tumoflip/test_workspaces_corpus.py").read_text())
    report = {"scope": "native profile validator / RAW parser only; excludes GUI, storage and hardware", "files": {}}
    for test, relative in CASES:
        method = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == test)
        call = next(n for n in ast.walk(method) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute) and n.func.attr == "native")
        source = ROOT / relative
        with tempfile.TemporaryDirectory(prefix="workspace-coverage-") as directory:
            tmp = Path(directory)
            fixture = tmp / "test.c"
            fixture.write_text(ast.literal_eval(call.args[0]))
            raw, profile, binary = tmp / "test.profraw", tmp / "test.profdata", tmp / "test"
            subprocess.run(["clang", "-std=c11", "-O0", "-g", "-fprofile-instr-generate", "-fcoverage-mapping",
                "-I", str(source.parent), str(fixture), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, env={**os.environ, "LLVM_PROFILE_FILE": str(raw)})
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", str(raw), "-o", str(profile)], check=True)
            coverage = json.loads(subprocess.check_output(["xcrun", "llvm-cov", "export", str(binary), f"-instr-profile={profile}"], text=True))
            item = next(f for f in coverage["data"][0]["files"] if Path(f["filename"]) == source)
            report["files"][relative] = item["summary"]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return all(f[m]["percent"] >= 80 for f in report["files"].values() for m in ("lines", "branches", "functions"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    raise SystemExit(0 if measure(parser.parse_args().output) else 1)
