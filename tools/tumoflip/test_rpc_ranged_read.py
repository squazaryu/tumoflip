"""Exercise production RPC reads; append-only schema, no GPS/network import."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class RpcRangedReadTests(unittest.TestCase):
    def test_ranges_and_failures(self):
        source = (ROOT / "applications/services/rpc/rpc_storage.c").read_text()
        fixture = (ROOT / "tools/tumoflip/fixtures/unleashed094/rpc_read.c").read_text()
        native(fixture.replace("/* @PRODUCTION@ */", function(source, "static void rpc_system_storage_read_process(")))

    def test_schema_is_a_storage_only_overlay(self):
        root = ROOT / "assets/protobuf_overrides"
        self.assertTrue((root / "storage.proto").is_file(), "storage range schema missing")
        schema = (root / "storage.proto").read_text()
        self.assertIn("uint32 offset = 2;", schema)
        self.assertIn("uint32 size = 3;", schema)
        self.assertIn("bool ranged = 2;", schema)
        for field in ("    uint32 offset = 2;\n    uint32 size = 3;\n", "    bool ranged = 2;\n"):
            schema = schema.replace(field, "")
        self.assertEqual(schema, (ROOT / "assets/protobuf/storage.proto").read_text())
        self.assertEqual({p.name for p in root.glob("*.proto")}, {"storage.proto"})
        self.assertEqual((root / "storage.options").read_text().rstrip("\n"), (ROOT / "assets/protobuf/storage.options").read_text().rstrip("\n"))


if __name__ == "__main__":
    unittest.main()
