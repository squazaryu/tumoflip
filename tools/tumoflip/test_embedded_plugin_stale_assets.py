"""Regression contracts for deterministic FAP assets and embedded plugin deps."""

import ast
import os
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))


def production_functions(relative_path, names, extra_globals=None):
    source_path = ROOT / relative_path
    tree = ast.parse(source_path.read_text(encoding="utf-8"))
    found = {
        node.name: node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name in names
    }
    missing = set(names) - found.keys()
    if missing:
        raise AssertionError(f"production helpers are missing: {sorted(missing)}")

    namespace = {"os": os, "pathlib": __import__("pathlib")}
    if extra_globals:
        namespace.update(extra_globals)
    module = ast.Module(body=[found[name] for name in names], type_ignores=[])
    exec(compile(module, str(source_path), "exec"), namespace)
    return namespace


class FakePlugin:
    def __init__(self, appid, fal_embedded, targets):
        self.appid = appid
        self.fal_embedded = fal_embedded
        self.targets = set(targets)

    def supports_hardware_target(self, target):
        return target in self.targets


class FakeDir:
    def __init__(self, root):
        self.root = Path(root)

    def File(self, name):
        return self.root / name


class EmbeddedPluginAssetTests(unittest.TestCase):
    def test_macos_junk_filter_is_narrow_and_explicit(self):
        helper = production_functions(
            "scripts/flipper/utils/__init__.py", ["is_macos_junk"]
        )["is_macos_junk"]

        for name in (".DS_Store", "._image.png", "._directory"):
            with self.subTest(name=name):
                self.assertTrue(helper(name))
        for name in ("image.png", "directory", "file._metadata", ".Spotlight-V100"):
            with self.subTest(name=name):
                self.assertFalse(helper(name))

    def test_file_bundler_prunes_sidecars_but_keeps_regular_assets(self):
        from fbt.fapassets import FileBundler

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "icons").mkdir()
            (root / ".DS_Store").write_bytes(b"finder")
            (root / "._icon.png").write_bytes(b"appledouble")
            (root / "icons" / "._old.png").write_bytes(b"appledouble")
            (root / "icons" / "keep.png").write_bytes(b"png")
            (root / "._junk").mkdir()
            (root / "._junk" / "ghost.txt").write_text("junk")

            bundler = FileBundler([str(root)])
            bundler._process_src_dirs()

            self.assertEqual([item["path"] for item in bundler.file_list], ["icons/keep.png"])
            self.assertEqual([item["path"] for item in bundler.directory_list], ["icons"])

    def test_manifest_omits_finder_sidecars(self):
        from flipper.assets.manifest import Manifest

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "._metadata").mkdir()
            (root / "._metadata" / "hidden.txt").write_text("hidden")
            (root / "._root-file").write_text("hidden")
            (root / "visible.txt").write_text("visible")
            manifest = Manifest(timestamp_value=1)
            manifest.create(str(root))

        paths = [record.path for record in manifest.records[2:]]
        self.assertEqual(paths, ["visible.txt"])

    def test_tarball_filter_skips_finder_sidecars(self):
        import tarfile
        from flipper.assets.tarball import tar_sanitizer_filter

        self.assertIsNone(tar_sanitizer_filter(tarfile.TarInfo(".DS_Store")))
        self.assertIsNone(tar_sanitizer_filter(tarfile.TarInfo("folder/._image.png")))
        self.assertEqual(tar_sanitizer_filter(tarfile.TarInfo("folder/image.png")).name,
                         "folder/image.png")

    def test_staged_embedded_dependencies_use_only_current_target_plugins(self):
        helpers = production_functions(
            "scripts/fbt_tools/fbt_extapps.py",
            ["_plugin_fal_name", "_embedded_plugin_fal_dependencies"],
        )
        plugins = [
            FakePlugin("decode_am", True, {"f7"}),
            FakePlugin("decode_subghz", True, {"f7", "f18"}),
            FakePlugin("external", False, {"f7"}),
            FakePlugin("other_target", True, {"f18"}),
        ]
        directory = FakeDir("/tmp/host/assets/plugins")

        dependencies = helpers["_embedded_plugin_fal_dependencies"](
            plugins, directory, "f7"
        )

        self.assertEqual(
            [path.name for path in dependencies], ["decode_am.fal", "decode_subghz.fal"]
        )
        self.assertTrue(all(path.parent == directory.root for path in dependencies))

    def test_regular_asset_walk_tracks_tree_shape_without_macos_junk(self):
        is_macos_junk = production_functions(
            "scripts/flipper/utils/__init__.py", ["is_macos_junk"]
        )["is_macos_junk"]
        collect = production_functions(
            "scripts/fbt_tools/fbt_extapps.py",
            ["_collect_asset_tree"],
            {"is_macos_junk": is_macos_junk},
        )["_collect_asset_tree"]

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "images").mkdir()
            (root / "images" / "keep.png").write_bytes(b"ok")
            (root / "images" / "._old.png").write_bytes(b"junk")
            (root / ".DS_Store").write_bytes(b"junk")
            (root / "._empty").mkdir()

            files, entries = collect(str(root))

        self.assertEqual([Path(path).relative_to(root).as_posix() for path in files],
                         ["images/keep.png"])
        self.assertEqual(entries, [("dir", "images"), ("file", "images/keep.png")])


if __name__ == "__main__":
    unittest.main()
