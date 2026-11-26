"""Execute build flag selection; FAP/updater/debug retain their original policy."""
import ast
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Environment(dict):
    def Append(self, **kwargs):
        for key, value in kwargs.items():
            self.setdefault(key, []).extend(value)

    def Replace(self, **kwargs):
        self.update(kwargs)


class ResidentSizeOptimizationTests(unittest.TestCase):
    def test_actual_flag_selection_changes_only_compact_resident_code(self):
        script = (ROOT / "site_scons/firmwareopts.scons").read_text().split("ENV.AppendUnique(", 1)[0]
        for resident, debug, compact, expected in (
            (True, False, True, "-Oz"), (False, False, True, "-Os"),
            (True, True, True, "-Og"), (True, False, False, "-Os")):
            env = Environment(IS_BASE_FIRMWARE=resident, DEBUG=debug, COMPACT=compact,
                              RESIDENT_SIZE_OPTIMIZATION="z")
            exec(script, {"Import": lambda *args: None, "ENV": env})
            self.assertEqual(env["CCFLAGS"], [expected])
        env = Environment(IS_BASE_FIRMWARE=True, DEBUG=False, COMPACT=True,
                          RESIDENT_SIZE_OPTIMIZATION="s")
        exec(script, {"Import": lambda *args: None, "ENV": env})
        self.assertEqual(env["CCFLAGS"], ["-Os"])

    def test_actual_external_app_normalization_keeps_fap_flags_and_libraries_on_os(self):
        tree = ast.parse((ROOT / "site_scons/extapps.scons").read_text())
        fn = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and
                  node.name == "normalize_external_size_optimization")
        scope = {}
        exec(compile(ast.Module(body=[fn], type_ignores=[]), "extapps.scons", "exec"), scope)
        original = {"Default": {"CCFLAGS": ["-Oz"], "CPPDEFINES": ["checked"]},
                    "flipper_application": {"CCFLAGS": ["-Og"]}}
        env = Environment(CCFLAGS=["-Wall", "-Oz"], CXXFLAGS=["-Oz"], FW_LIB_OPTS=original,
                          RESIDENT_SIZE_OPTIMIZATION="z")
        scope[fn.name](env)
        self.assertEqual(env["CCFLAGS"], ["-Wall", "-Os"])
        self.assertEqual(env["CXXFLAGS"], ["-Os"])
        self.assertEqual(env["FW_LIB_OPTS"]["Default"]["CCFLAGS"], ["-Os"])
        self.assertEqual(env["FW_LIB_OPTS"]["Default"]["CPPDEFINES"], ["checked"])
        self.assertEqual(env["FW_LIB_OPTS"]["flipper_application"]["CCFLAGS"], ["-Og"])
        self.assertEqual(original["Default"]["CCFLAGS"], ["-Oz"])


if __name__ == "__main__":
    unittest.main()
