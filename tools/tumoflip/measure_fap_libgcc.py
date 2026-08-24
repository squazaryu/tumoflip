#!/usr/bin/env python3
"""Measure toolchain-helper residency in FAP build artifacts.

This is deliberately a read-only measurement tool. It does not change the FAP ABI or mark any
application to consume firmware-provided libgcc symbols; that decision belongs to the API-88.4
compatibility gate tracked in issue #345.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Iterable


# Keep this list aligned with the helper symbols proposed by upstream API 88.4. A broad
# ``__mul*``/``__div*`` prefix would also count application/library routines such as mbedTLS's
# ``__multiply`` and ``__multadd`` and make the baseline misleading.
HELPER_RE = re.compile(
    r"^(?:__aeabi_[A-Za-z0-9_]+|__(?:adddf3|cmpdf2|divdf3|eqdf2|extendsfdf2|fixdfdi|fixdfsi|"
    r"fixunsdfdi|fixunsdfsi|floatdidf|floatsidf|floatundidf|floatunsidf|gedf2|gtdf2|ledf2|"
    r"ltdf2|muldf3|nedf2|paritysi2|popcountsi2|subdf3|truncdfsf2|udivmoddi4|unorddf2))$"
)
API_VERSION_RE = re.compile(r"^Version,\+,([^,]+),,", re.MULTILINE)


def parse_nm_output(output: str) -> list[dict[str, int | str]]:
    """Return defined compiler-helper symbols from GNU nm ``-S`` output."""

    helpers: list[dict[str, int | str]] = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 4:
            continue
        address, size, symbol_type, name = fields[:4]
        if symbol_type.upper() == "U" or not HELPER_RE.match(name):
            continue
        try:
            symbol_size = int(size, 16)
        except ValueError:
            continue
        helpers.append({"name": name, "size": symbol_size})
    return sorted(helpers, key=lambda item: (str(item["name"]), int(item["size"])))


def api_version(api_symbols: Path) -> str:
    match = API_VERSION_RE.search(api_symbols.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"API version row not found: {api_symbols}")
    return match.group(1)


def measure_artifact(nm: str, artifact: Path) -> dict[str, object]:
    result = subprocess.run(
        [nm, "-S", "--defined-only", str(artifact)],
        check=True,
        capture_output=True,
        text=True,
    )
    helpers = parse_nm_output(result.stdout)
    return {
        "path": str(artifact),
        "bytes": artifact.stat().st_size,
        "helper_bytes": sum(int(item["size"]) for item in helpers),
        "helpers": helpers,
    }


def build_report(nm: str, api_symbols: Path, artifacts: Iterable[Path]) -> dict[str, object]:
    entries = [measure_artifact(nm, path) for path in sorted(artifacts, key=str)]
    return {
        "schema": 1,
        "api_version": api_version(api_symbols),
        "artifacts": entries,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+", type=Path, help="ELF/FAP artifacts to inspect")
    parser.add_argument(
        "--nm",
        default=shutil.which("arm-none-eabi-nm") or "arm-none-eabi-nm",
        help="GNU nm executable (default: arm-none-eabi-nm)",
    )
    parser.add_argument(
        "--api-symbols",
        type=Path,
        default=Path("targets/f7/api_symbols.csv"),
        help="API symbol table used for the compatibility record",
    )
    args = parser.parse_args()
    report = build_report(args.nm, args.api_symbols, args.artifacts)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
