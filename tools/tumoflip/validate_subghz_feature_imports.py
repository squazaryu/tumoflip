"""Resolve feature FAL imports against firmware plus the exact Sub-GHz host table."""
import argparse
import csv
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def validate(build, nm):
    table = (ROOT / "applications/main/subghz/api/subghz_app_api_table_i.h").read_text()
    private = set(re.findall(r"API_(?:METHOD|VARIABLE)\(\s*(\w+)\s*,", table))
    with (ROOT / "targets/f7/api_symbols.csv").open() as stream:
        public = {row[2] for row in csv.reader(stream)
                  if len(row) >= 3 and row[0] in ("Function", "Variable") and row[1] == "+"}
    defined = subprocess.check_output([nm, "--defined-only", str(build / "firmware.elf")], text=True)
    definitions = {line.split()[-1] for line in defined.splitlines() if line.split()}
    missing_exports = private - definitions
    if missing_exports:
        raise ValueError(f"Host declarations missing from resident ELF: {sorted(missing_exports)}")
    for app in ("subghz_frequency_analyzer", "subghz_add_manually"):
        output = subprocess.check_output(
            [nm, "-u", str(build / ".extapps" / (app + ".fal"))], text=True)
        imports = {line.split()[-1] for line in output.splitlines() if line.split()}
        missing = imports - public - private
        if missing:
            raise ValueError(f"{app}: unresolved imports {sorted(missing)}")
        print(f"{app}: {len(imports)} imports resolved, {len(imports & private)} via Sub-GHz host")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build/f7-firmware-C")
    parser.add_argument("--nm", default=str(ROOT / "toolchain/current/bin/arm-none-eabi-nm"))
    args = parser.parse_args()
    validate(args.build, args.nm)
