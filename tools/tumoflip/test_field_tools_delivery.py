"""Real application manifest routing; never hide field tools in core flash."""
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


class FieldToolsDeliveryTests(unittest.TestCase):
    def test_new_faps_reach_the_actual_base_package_manifest(self):
        from tools.tumoflip.test_package_release import prepare_package_tree
        from tools.tumoflip.validate_release import package_entries
        with tempfile.TemporaryDirectory(prefix="field-package-contract-") as directory:
            _, _, resources = prepare_package_tree(Path(directory))
            paths = {entry["source"] for entry in package_entries(resources)["base"]}
            self.assertIn("apps/Tools/field_tools.fap", paths)
            self.assertIn("apps/NFC/tumo_nfc_sniffer.fap", paths)

    def test_new_apps_are_base_packages_not_updater_payloads(self):
        from tools.tumoflip import validate_release as release
        expected = {"field_tools.fap": "apps/Tools/field_tools.fap",
                    "tumo_nfc_sniffer.fap": "apps/NFC/tumo_nfc_sniffer.fap"}
        for filename, path in expected.items():
            self.assertEqual(release.package_extapp_exports().get(filename), path)
            self.assertIn(path, release.PACKAGE_ONLY_PACKAGE_FILES)
            self.assertEqual(release.PACKAGE_ONLY_PACKAGE_GROUPS[path], "base")
        for stem in ("rx_x10", "rx_urmet", "rx_gm0", "rx_gm1", "rx_ty0", "rx_mz2", "rx_mb0", "rx_mb1", "rx_op1", "rx_bmw"):
            self.assertIn("protocol_" + stem + ".fal", release.PROTOCOL_PACKS)

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
