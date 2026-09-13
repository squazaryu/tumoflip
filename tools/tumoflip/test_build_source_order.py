"""Build-source contracts: ordered globs and folder-wide exclusions."""
import ast
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest

from scripts.fbt.appmanifest import AppBuildset

ROOT = Path(__file__).resolve().parents[2]


def gather_source():
    module = ast.parse((ROOT / "scripts/fbt_tools/sconsrecursiveglob.py").read_text())
    return ast.unparse(next(n for n in module.body if isinstance(n, ast.FunctionDef) and n.name == "GatherSources"))


class BuildSourceOrderTests(unittest.TestCase):
    def test_pattern_order_does_not_depend_on_python_hash_seed(self):
        program = "import itertools,json\nfrom types import SimpleNamespace\nFlatten=lambda x:x\n"
        program += gather_source()
        program += '\nenv=SimpleNamespace(GlobRecursive=lambda pattern,node,exclude:[pattern])\n'
        program += 'print(json.dumps(GatherSources(env,["b.c","a.c","b.c","c.c"])))\n'
        for seed in (0, 1, 2, 42, 999):
            with self.subTest(seed=seed):
                result = subprocess.check_output(
                    [sys.executable, "-c", program],
                    env={**os.environ, "PYTHONHASHSEED": str(seed)}, text=True,
                )
                self.assertEqual(json.loads(result), ["b.c", "a.c", "c.c"])

    def test_startup_hook_does_not_reintroduce_excluded_plugins(self):
        build = object.__new__(AppBuildset)
        build.get_builtin_apps = lambda: [
            SimpleNamespace(_appdir="subghz", sources=["*.c*", "!plugins"]),
            SimpleNamespace(_appdir="subghz", sources=["*.c*"]),
            SimpleNamespace(_appdir="another", sources=["*.c*"]),
        ]
        folders = build.get_builtin_app_folders()
        self.assertEqual(len(folders), 2)
        patterns = dict(folders)
        self.assertEqual(set(patterns["subghz"]), {"*.c*", "!plugins"})
        self.assertNotIn("!plugins", patterns["another"])
        namespace = {"Flatten": lambda value: value, "itertools": itertools}
        exec(gather_source(), namespace)
        seen = []
        env = SimpleNamespace(GlobRecursive=lambda pattern,node,exclude: seen.append((node,tuple(exclude))) or [node+"/main.c"])
        namespace["GatherSources"](env, [*patterns["subghz"], "!lib"], "subghz")
        self.assertEqual(seen, [("subghz", ("plugins", "lib"))])


if __name__ == "__main__":
    unittest.main()
