"""Host and source regressions for reserved RPC launch contexts."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RPC_APP = ROOT / "applications/services/rpc/rpc_app.c"
RPC_SERVICE = ROOT / "applications/services/rpc/rpc.c"


def function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class RpcAppContextSecurityTests(unittest.TestCase):
    def test_reserved_context_argument_is_rejected_before_app_launch(self) -> None:
        source = RPC_APP.read_text(encoding="utf-8")
        body = function_body(source, "static void rpc_system_app_start_process(")

        self.assertTrue(
            "rpc_app_args_forbid_context(app_args)" in body,
            "untrusted RPC context arguments must be rejected",
        )
        self.assertLess(
            body.index("rpc_app_args_forbid_context(app_args)"),
            body.index("loader_start(loader, app_name, app_args, NULL)"),
        )
        self.assertIn("PB_CommandStatus_ERROR_INVALID_PARAMETERS", body)

    def test_live_context_is_registered_and_validated_under_a_mutex(self) -> None:
        source = RPC_APP.read_text(encoding="utf-8")
        service = RPC_SERVICE.read_text(encoding="utf-8")
        allocate = function_body(source, "void* rpc_system_app_alloc(")
        release = function_body(source, "void rpc_system_app_free(")
        callback = function_body(source, "void rpc_system_app_set_callback(")
        start = function_body(service, "void rpc_on_system_start(")

        self.assertIn("rpc_system_app_init();", start)
        self.assertIn("rpc_system_app_instance_register(rpc_app);", allocate)
        self.assertIn("rpc_system_app_instance_unregister(rpc_app);", release)
        self.assertLess(
            release.index("rpc_system_app_instance_unregister(rpc_app)"),
            release.index("free(rpc_app)"),
        )
        self.assertIn("rpc_system_app_instances_lock();", callback)
        self.assertIn("rpc_system_app_instance_is_valid(rpc_app)", callback)
        self.assertLess(
            callback.index("rpc_system_app_instance_is_valid(rpc_app)"),
            callback.index("rpc_app->callback = callback"),
        )
        self.assertIn("rpc_system_app_instances_unlock();", callback)

    def test_reserved_prefix_parser_accepts_only_the_internal_token(self) -> None:
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")

        program = r'''
#include "applications/services/rpc/rpc_app_context_guard.h"

int main(void) {
    if(rpc_app_args_forbid_context(NULL)) return 1;
    if(rpc_app_args_forbid_context("RPC")) return 2;
    if(!rpc_app_args_forbid_context("RPC 1234ABCD")) return 3;
    if(!rpc_app_args_forbid_context("RPC\t1234ABCD")) return 4;
    if(!rpc_app_args_forbid_context("RPC\n1234ABCD")) return 5;
    if(rpc_app_args_forbid_context("RPCX")) return 6;
    if(rpc_app_args_forbid_context("ordinary RPC 1234")) return 7;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "rpc_context.c"
            executable = Path(directory) / "rpc_context"
            fixture.write_text(program, encoding="utf-8")
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                 str(fixture), "-o", str(executable)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
