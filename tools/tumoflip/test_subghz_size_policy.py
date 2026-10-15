"""Keep a size-oriented protocol library separate from HAL/loader build policy."""
import ast
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SubGhzSizePolicyTests(unittest.TestCase):
    def test_protocol_size_policy_keeps_debug_and_driver_defines(self):
        tree = ast.parse((ROOT / "firmware.scons").read_text())
        nodes = [node.value for node in ast.walk(tree)
                 if isinstance(node, ast.keyword) and node.arg == "FW_LIB_OPTS"]
        self.assertEqual(len(nodes), 1)
        expression = compile(ast.Expression(nodes[0]), "firmware.scons", "eval")
        for debug in (False, True):
            options = eval(expression, {"__builtins__": {}, "ENV": {"DEBUG": debug, "LIB_DEBUG": debug}})
            self.assertIn("subghz", options, "protocol library has no scoped size policy")
            self.assertEqual(options["subghz"]["CCFLAGS"], ["-Og" if debug else "-Oz"])
            self.assertEqual(options["subghz"]["CPPDEFINES"], options["Default"]["CPPDEFINES"])
            self.assertEqual(options["Default"]["CCFLAGS"], ["-Og" if debug else "-Os"])
            self.assertEqual(options["furi"]["CCFLAGS"], ["-Os"])
            self.assertEqual(options["flipper_application"]["CCFLAGS"], ["-Og"])


if __name__ == "__main__":
    unittest.main()
