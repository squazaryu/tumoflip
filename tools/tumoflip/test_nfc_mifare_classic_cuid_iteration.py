#!/usr/bin/env python3
"""Regression coverage for MIFARE Classic CUID key-index iteration."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
POLLER = REPO_ROOT / "lib/nfc/protocols/mf_classic/mf_classic_poller.c"
HOST_TEST = (
    REPO_ROOT / "tools/tumoflip/fixtures/nfc_mf_classic_cuid_iteration_host_test.c"
)


def function_body(contents: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        contents,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class NfcMifareClassicCuidIterationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.poller = POLLER.read_text(encoding="utf-8")

    def test_cuid_transition_matrix_with_host_compiler(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "a host C compiler is required")

        with tempfile.TemporaryDirectory(prefix="tumoflip-cuid-") as directory:
            executable = Path(directory) / "cuid_iteration_test"
            subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(REPO_ROOT),
                    str(HOST_TEST),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=REPO_ROOT)

    def test_known_cuid_keys_advance_the_app_owned_cursor(self) -> None:
        for signature in (
            "mf_classic_poller_handler_auth_a(",
            "mf_classic_poller_handler_auth_b(",
        ):
            body = function_body(self.poller, signature)
            cuid = body.index("MfClassicPollerModeDictAttackCUID")
            next_sector = body.index("MfClassicPollerStateNextSector", cuid)
            self.assertLess(cuid, next_sector)

    def test_failed_cuid_auth_retries_the_same_key_index(self) -> None:
        for signature in (
            "mf_classic_poller_handler_auth_a(",
            "mf_classic_poller_handler_auth_b(",
        ):
            body = function_body(self.poller, signature)
            auth_result = body.index("if(error == MfClassicErrorNone)")
            failure = body.index("} else {", auth_result)
            request_key = body.index("MfClassicPollerStateRequestKey", failure)

            self.assertNotIn(
                "MfClassicPollerStateNextSector", body[failure:request_key]
            )

    def test_exhausted_dictionary_advances_the_cursor(self) -> None:
        body = function_body(self.poller, "mf_classic_poller_handler_request_key(")
        provided = body.index("key_request_data.key_provided")
        exhausted = body.index("} else {", provided)
        next_sector = body.index("MfClassicPollerStateNextSector", exhausted)

        self.assertLess(exhausted, next_sector)

    def test_cuid_callback_runs_before_completion_check(self) -> None:
        body = function_body(self.poller, "mf_classic_poller_handler_next_sector(")
        cuid = body.index("MfClassicPollerModeDictAttackCUID")
        callback = body.index("instance->callback", cuid)
        read_back = body.index(
            "instance->mfc_event_data.next_sector_data.current_sector", callback
        )
        completion = body.index(">= instance->sectors_total", read_back)
        cuid_return = body.index("return command;", completion)
        normal_increment = body.index("dict_attack_ctx->current_sector++;", cuid_return)

        self.assertLess(callback, read_back)
        self.assertLess(read_back, completion)
        self.assertLess(cuid_return, normal_increment)

    def test_non_cuid_iteration_keeps_sector_increment(self) -> None:
        body = function_body(self.poller, "mf_classic_poller_handler_next_sector(")
        self.assertEqual(body.count("dict_attack_ctx->current_sector++;"), 1)
        self.assertIn(
            "dict_attack_ctx->current_sector == instance->sectors_total", body
        )


if __name__ == "__main__":
    unittest.main()
