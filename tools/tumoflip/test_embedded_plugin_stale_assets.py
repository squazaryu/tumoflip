"""Guard generated embedded-FAL staging deps without hiding legitimate assets."""

import ast
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "scripts/fbt_tools/fbt_extapps.py"


def production_filter():
    tree = ast.parse(SOURCE.read_text(encoding="utf-8"))
    definition = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "_filter_stale_embedded_plugin_assets"
    )
    namespace = {"pathlib": __import__("pathlib")}
    exec(compile(ast.Module(body=[definition], type_ignores=[]), str(SOURCE), "exec"), namespace)
    return namespace[definition.name]


class FakeNode:
    def __init__(self, path: Path, has_builder: bool):
        self.abspath = str(path)
        self._has_builder = has_builder

    def has_builder(self):
        return self._has_builder


class EmbeddedPluginStaleAssetsTests(unittest.TestCase):
    def test_only_unbuilt_fals_in_the_exact_embedded_plugin_tree_are_filtered(self):
        filter_assets = production_filter()
        root = Path("/tmp/fw/apps_work/subghz/assets/plugins")
        stale_fal = FakeNode(root / "old.fal", False)
        active_fal = FakeNode(root / "current.fal", True)
        icon = FakeNode(root / "icon.png", False)
        ordinary_asset = FakeNode(root.parent / "palette.bin", False)
        unrelated_plugins_dir = FakeNode(root.parent.parent / "other/plugins/addon.fal", False)

        actual = filter_assets(
            [stale_fal, active_fal, icon, ordinary_asset, unrelated_plugins_dir], root
        )

        self.assertEqual(actual, [active_fal, icon, ordinary_asset, unrelated_plugins_dir])


if __name__ == "__main__":
    unittest.main()
