#!/usr/bin/env python3
"""Regression contracts for non-blocking USB CLI log streaming."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = REPO_ROOT / "applications/services/cli/cli_main_commands.c"


def function_body(source: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        source,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class CliLogBackpressureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")

    def test_log_callback_never_waits_for_pipe_capacity(self) -> None:
        callback = function_body(self.source, "void cli_command_log_tx_callback(")

        self.assertIn("pipe_spaces_available(pipe)", callback)
        self.assertIn("if(available < size)", callback)
        self.assertIn("return;", callback)
        self.assertLess(
            callback.index("if(available < size)"),
            callback.index("pipe_send(pipe, buffer, size)"),
        )

    def test_log_callback_preserves_complete_fragments_when_capacity_is_available(self) -> None:
        callback = function_body(self.source, "void cli_command_log_tx_callback(")

        self.assertIn("pipe_send(pipe, buffer, size)", callback)
        self.assertIn("if(available < size) return;", callback)


if __name__ == "__main__":
    unittest.main()
