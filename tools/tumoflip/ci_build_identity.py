#!/usr/bin/env python3
"""Choose diagnostic Dev or exact Stable identity for the pre-tag CI build."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.tumoflip.sync_readme_version import parse_dist_suffix, read_dist_suffix
from tools.tumoflip.validate_stable_release import parse_stable_serial


def build_identity(version: str, sha: str, ref: str) -> tuple[str, str]:
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError("CI requires a complete lowercase source SHA")
    if not re.fullmatch(r"[A-Za-z0-9._-]{1,200}", ref):
        raise ValueError("CI requires a sanitized, non-empty workflow ref")
    if not re.fullmatch(r"[A-Za-z0-9-]+", version):
        raise ValueError("CI requires a single literal firmware identity")
    prefix, _, _, _ = parse_dist_suffix(version)
    if prefix == "t-dev":
        return sha[:8], ref
    serial = parse_stable_serial(version)
    return version, f"v1.0.{serial}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=None)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--ref", required=True)
    args = parser.parse_args()
    try:
        dist, ref = build_identity(args.version or read_dist_suffix(), args.sha, args.ref)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    print(f"firmware_dist={dist}")
    print(f"workflow_ref={ref}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
