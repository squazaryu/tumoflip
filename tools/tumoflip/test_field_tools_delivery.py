"""Real application manifest routing; never hide field tools in core flash."""
from pathlib import Path
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


class FieldToolsDeliveryTests(unittest.TestCase):
    def test_field_tools_is_a_buildable_package_only_application(self):
        apps = []
        exec(compile((APP / "application.fam").read_text(), "application.fam", "exec"), {
            "App": lambda **values: apps.append(values),
            "FlipperAppType": SimpleNamespace(EXTERNAL=1, PLUGIN=2),
        })
        selected = [a for a in apps if a["appid"] == "field_tools"]
        self.assertEqual(len(selected), 1, "Field tools must be a package-only, buildable FAP")
        app = selected[0]
        self.assertTrue(app["fap_package_only"])
        for source in app["sources"]:
            self.assertTrue((APP / source).is_file(), source)
        self.assertIn("field_inventory.c", app["sources"])
        self.assertIn("field_bundle.c", app["sources"])
        self.assertIn("field_round.c", app["sources"])


if __name__ == "__main__":
    unittest.main()
