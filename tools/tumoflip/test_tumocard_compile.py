#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

from tools.tumoflip.tumocard_compile import CompileError, compile_applet


class TumoCardCompileTest(unittest.TestCase):
    def write_definition(self, root: Path, **overrides: object) -> Path:
        definition: dict[str, object] = {
            "id": "demo_token",
            "name": "Demo Token",
            "aid": "F0 54 43 41 52 44 10",
            "initial_state": "44 45 4D 4F",
            "profile": "state-v1",
            "writable": True,
            "enabled": True,
            "capabilities": ["nfc.type4", "usb.ccid"],
            "crypto": [],
        }
        definition.update(overrides)
        path = root / "applet.json"
        path.write_text(json.dumps(definition), encoding="utf-8")
        return path

    def test_compiles_bounded_state_applet(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            destination = compile_applet(self.write_definition(root), root / "applets")
            self.assertEqual(destination.name, "demo_token")
            manifest = (destination / "manifest.tca").read_text(encoding="ascii")
            program = (destination / "program.tvm").read_text(encoding="ascii")
            settings = (destination / "settings.tcs").read_text(encoding="ascii")
            self.assertIn("AID size: 7", manifest)
            self.assertIn("Crypto mask: 0", manifest)
            self.assertIn("Route count: 3", program)
            self.assertIn("00 D6 0C", program)
            self.assertIn("Enabled: true", settings)

    def test_read_only_profile_omits_update_route(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            destination = compile_applet(
                self.write_definition(root, writable=False), root / "applets"
            )
            program = (destination / "program.tvm").read_text(encoding="ascii")
            self.assertIn("Route count: 2", program)
            self.assertNotIn("00 D6", program)

    def test_rejects_unsupported_or_unbounded_definitions(self) -> None:
        invalid_cases = (
            {"id": "Bad/Id"},
            {"initial_state": "AA " * 65},
            {"capabilities": ["nfc.type4", "usb.ccid", "gpio.raw"]},
            {"crypto": ["aes.private-key"]},
            {"profile": "native-code"},
        )
        for overrides in invalid_cases:
            with self.subTest(overrides=overrides), tempfile.TemporaryDirectory() as temp_dir:
                root = Path(temp_dir)
                with self.assertRaises(CompileError):
                    compile_applet(self.write_definition(root, **overrides), root / "applets")

    def test_does_not_overwrite_without_force(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            definition = self.write_definition(root)
            compile_applet(definition, root / "applets")
            with self.assertRaises(CompileError):
                compile_applet(definition, root / "applets")
            replaced = compile_applet(definition, root / "applets", force=True)
            self.assertTrue((replaced / "manifest.tca").is_file())


if __name__ == "__main__":
    unittest.main()
