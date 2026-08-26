#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class RpcLoaderStartTests(unittest.TestCase):
    def test_every_loader_status_is_mapped_without_crashing(self) -> None:
        loader_header = (
            REPO_ROOT / "applications/services/loader/loader.h"
        ).read_text(encoding="utf-8")
        rpc_source = (
            REPO_ROOT / "applications/services/rpc/rpc_app.c"
        ).read_text(encoding="utf-8")
        body = function_body(rpc_source, "rpc_system_app_start_process")

        enum_body = re.search(
            r"typedef enum \{(?P<body>.*?)\} LoaderStatus;",
            loader_header,
            re.DOTALL,
        )
        self.assertIsNotNone(enum_body)
        statuses = re.findall(r"\bLoaderStatus[A-Za-z0-9_]+\b", enum_body.group("body"))

        self.assertEqual(len(statuses), 6)
        for status in statuses:
            self.assertIn(status, body)
        self.assertIn(
            "switch(loader_start_with_diagnostic(loader, app_name, app_args, NULL, &diagnostic))",
            body,
        )
        self.assertIn("rpc_system_app_set_error_code", body)
        self.assertIn("rpc_system_app_set_error_text", body)
        self.assertIn("result = PB_CommandStatus_ERROR_APP_CANT_START", body)
        self.assertNotIn("furi_crash", body)


if __name__ == "__main__":
    unittest.main()
