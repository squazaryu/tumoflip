"""Measure the production envelope statistics and radio controller, not GUI mocks."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

from tools.tumoflip import test_rssi_envelope as regression
from tools.tumoflip.test_hotplug_assets import function

APP = regression.APP


def measure(output):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    programs = []
    saved = regression.native
    try:
        regression.native = programs.append
        result = unittest.TextTestRunner(verbosity=0).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(regression.RssiEnvelopeTests))
        if not result.wasSuccessful():
            raise RuntimeError("Cannot collect the current regression programs")
    finally:
        regression.native = saved
    driver = next(p for p in programs if "struct TumoSpectrumEnvelope {" in p)
    combined = driver.replace("int main(void)", "static int envelope_case_radio(void)")
    cases = ["envelope_case_radio"]
    for program in programs:
        if program == driver or "tumospectrum_envelope_input(" in program:
            continue  # UI callback mocks are not production-radio coverage.
        name = f"envelope_case_{len(cases)}"
        combined += "\n" + function(program, "int main(void)").replace("int main(void)", f"static int {name}(void)")
        cases.append(name)
    combined += "\nint main(void){" + "".join(f"assert({name}()==0);" for name in cases) + "return 0;}\n"
    source, binary = output / "envelope.c", output / "envelope-test"
    source.write_text(combined)
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O0", "-g",
        "-fsanitize=address,undefined", "-fprofile-instr-generate", "-fcoverage-mapping",
        str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, env={**os.environ, "LLVM_PROFILE_FILE": str(output / "run.profraw")})
    profile = output / "run.profdata"
    subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", str(output / "run.profraw"), "-o", str(profile)], check=True)
    report = json.loads(subprocess.check_output([
        "xcrun", "llvm-cov", "export", str(binary), f"-instr-profile={profile}"], text=True))
    lcov = subprocess.check_output([
        "xcrun", "llvm-cov", "export", str(binary), f"-instr-profile={profile}", "-format=lcov"], text=True)
    measured = {}
    relevant_file = False
    for line in lcov.splitlines():
        if line.startswith("SF:"):
            relevant_file = line[3:] == str(source)
        elif relevant_file and line.startswith("DA:"):
            number, count = map(int, line[3:].split(",")[:2])
            measured[number] = count
    summary = {}
    for name in ("tumospectrum_envelope_stats.c", "tumospectrum_envelope.c"):
        production = (APP / name).read_text()
        functions = [item for item in report["data"][0]["functions"] if re.search(
            r"(?m)^[A-Za-z_][\w \t*]*\b" + re.escape(item["name"].rsplit(":", 1)[-1])
            + r"\s*\([^;{}]*\)\s*\{", production)]
        if not functions:
            raise RuntimeError(f"No measured production bodies: {name}")
        regions = [r for f in functions for r in f["regions"] if r[7] == 0]
        relevant = set()
        for item in functions:
            start, _, end, *_ = item["regions"][0]
            relevant.update(range(start, end + 1))
        lines = [value for number, value in measured.items() if number in relevant]
        if not lines:
            raise RuntimeError(f"No production line mapping: {name}")
        summary[name] = {
            "functions": len(functions), "functions_hit": sum(f["count"] > 0 for f in functions),
            "lines_percent": round(100 * sum(value > 0 for value in lines) / len(lines), 2),
            "regions_percent": round(100 * sum(r[4] > 0 for r in regions) / len(regions), 2),
        }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    return all(min(item["lines_percent"], item["regions_percent"]) >= 80 for item in summary.values())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    raise SystemExit(0 if measure(parser.parse_args().output) else 1)
