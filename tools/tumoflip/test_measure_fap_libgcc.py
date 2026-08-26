#!/usr/bin/env python3
"""Unit contracts for the read-only #345 FAP memory measurement tool."""

from pathlib import Path
import tempfile
import unittest

try:
    from .measure_fap_libgcc import build_report, parse_nm_output
except ImportError:  # Supports unittest discovery from the tools directory.
    from measure_fap_libgcc import build_report, parse_nm_output


REPO_ROOT = Path(__file__).resolve().parents[2]


class MeasureFapLibgccTest(unittest.TestCase):
    def test_parser_keeps_defined_helpers_and_ignores_undefined_symbols(self) -> None:
        output = """
00000000 00000010 T __aeabi_uldivmod
00000010 00000004 t __adddf3
00000014 00000000 U __aeabi_f2d
00000018 00000020 T __multiply
00000038 00000020 T application_function
"""
        self.assertEqual(
            parse_nm_output(output),
            [
                {"name": "__adddf3", "size": 4},
                {"name": "__aeabi_uldivmod", "size": 16},
            ],
        )

    def test_report_records_api_version_and_artifact_size(self) -> None:
        api = REPO_ROOT / "targets/f7/api_symbols.csv"
        with tempfile.TemporaryDirectory(prefix="tumoflip-libgcc-") as directory:
            artifact = Path(directory) / "measure-fap-libgcc.elf"
            artifact.write_bytes(b"fixture")
            report = build_report(
                "echo",
                api,
                [artifact],
            )

        self.assertEqual(report["schema"], 1)
        self.assertEqual(report["api_version"], "88.5")
        self.assertEqual(report["artifacts"][0]["bytes"], 7)


if __name__ == "__main__":
    unittest.main()
