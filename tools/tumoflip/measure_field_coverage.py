"""Coverage of production field engines, excluding native I/O/GUI substitutes."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


def measure(output):
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "-m", "unittest", "tools.tumoflip.test_field_rounds",
                    "tools.tumoflip.test_field_bundle", "tools.tumoflip.test_field_inventory"],
                   check=True, cwd=ROOT, env={**os.environ, "TUMO_FIELD_COVERAGE_DIR": str(output.resolve())})
    profile = output / "total.profdata"
    subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", *map(str, output.glob("*.profraw")), "-o", str(profile)], check=True)
    report = json.loads(subprocess.check_output(["xcrun", "llvm-cov", "export", str(output / "last-test"), "-instr-profile=" + str(profile)], text=True))
    lcov = subprocess.check_output(["xcrun", "llvm-cov", "export", str(output / "last-test"),
                                    "-instr-profile=" + str(profile), "-format=lcov"], text=True)
    measured_lines = {}
    fixture = False
    for line in lcov.splitlines():
        if line.startswith("SF:"):
            fixture = line.endswith("/test.c")
        if fixture and line.startswith("DA:"):
            number, count = map(int, line[3:].split(",")[:2])
            measured_lines[number] = count
    summary = {}
    for filename in ("field_io.c", "field_round.c", "field_bundle.c", "field_inventory.c"):
        production = (APP / filename).read_text()
        functions = [item for item in report["data"][0]["functions"] if re.search(
            r"\b" + re.escape(item["name"].rsplit(":", 1)[-1]) + r"\s*\([^;{}]*\)\s*\{", production)]
        if not functions:
            raise RuntimeError("No measured production bodies: " + filename)
        regions = [r for f in functions for r in f["regions"] if r[7] == 0]
        relevant = set()
        for item in functions:
            start, _, end, *_ = item["regions"][0]
            relevant.update(range(start, end + 1))
        lines = [value for number, value in measured_lines.items() if number in relevant]
        if not lines:
            raise RuntimeError("No production line mapping: " + filename)
        summary[filename] = {"functions": len(functions), "functions_hit": sum(f["count"] > 0 for f in functions),
                             "lines_percent": round(100 * sum(value > 0 for value in lines) / len(lines), 2),
                             "regions_percent": round(100 * sum(r[4] > 0 for r in regions) / len(regions), 2),
                             "unhit": [f["name"].rsplit(":", 1)[-1] for f in functions if not f["count"]]}
    (output / "coverage.json").write_text(json.dumps(report))
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return all(min(item["regions_percent"], item["lines_percent"]) >= 80 and item["functions_hit"] / item["functions"] >= .8 for item in summary.values())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    raise SystemExit(0 if measure(parser.parse_args().output) else 1)
