"""Disposable distribution action; never operates outside the fixture cwd."""

import argparse
import fcntl
from pathlib import Path
import shutil


parser = argparse.ArgumentParser()
parser.add_argument("operation", choices=["copy"])
parser.add_argument("-p")
parser.add_argument("-s")
parser.add_argument("--kind", required=True)
args = parser.parse_args()

# Serialize fake wipers to isolate the FAP/install ordering race from unrelated
# interactions between two distribution commands. This is fixture-only code.
with Path("dist-fixture.lock").open("w") as lock:
    fcntl.flock(lock, fcntl.LOCK_EX)
    output = Path("dist/f7-C")
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    (output / "firmware.bin").write_bytes(b"distributed firmware")
    with Path("dist-events.txt").open("a") as events:
        events.write(args.kind + "\n")
