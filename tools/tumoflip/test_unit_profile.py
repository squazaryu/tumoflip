"""The core on-device profile must not resolve private symbols from an excluded FAP."""

import json
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]


class UnitProfileTests(unittest.TestCase):
    def test_core_test_api_has_no_excluded_javascript_host_dependency(self):
        api = (ROOT / "applications/debug/unit_tests/unit_test_api_table_i.h").read_text()
        self.assertNotIn("js_app/", api)
        self.assertNotRegex(api, r"\bjs_(?:thread|value)_")
        self.assertIn("API_VARIABLE(PB_Main_msg", api)
        self.assertIn("mf_plus_crypto_cmac", api)

    def test_unit_preset_does_not_make_javascript_resident_or_enable_its_fap(self):
        options = (ROOT / "fbt_options.py").read_text()
        unit = options.split('"unit_tests": [', 1)[1].split("]", 1)[0]
        self.assertNotIn('"js_app"', unit)
        excluded = options.split("EXCLUDED_EXT_APPS = (", 1)[1]
        self.assertIn('"js_app"', excluded)

    def test_test_js_is_preserved_outside_the_core_manifest_and_resource_tree(self):
        manifest = (ROOT / "applications/debug/unit_tests/application.fam").read_text()
        self.assertNotIn('appid="test_js"', manifest)
        archive = ROOT / "tools/tumoflip/fixtures/deferred_js_tests"
        evidence = json.loads((archive / "origin.json").read_text())
        import hashlib
        for item in evidence["files"]:
            path = archive / item["saved"]
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), item["sha256"])
        self.assertFalse((ROOT / "applications/debug/unit_tests/resources/unit_tests/js").exists())

    def test_projector_fixture_uses_the_real_asset_and_all_button_names(self):
        source = (ROOT / "applications/debug/unit_tests/tests/infrared/infrared_test.c").read_text()
        test = function(source, "MU_TEST(infrared_test_projector_database)")
        filename = re.search(r'EXT_PATH\("([^"]+)"\)', test).group(1)
        asset = ROOT / "applications/main/infrared/resources" / filename
        self.assertTrue(asset.is_file(), f"unit fixture points to absent {filename}")
        shipped = set(re.findall(r"^name:\s*(.+)$", asset.read_text(), re.M))
        registered = set(re.findall(r'add_record\([^;]+"([^"]+)"\)', test))
        self.assertTrue(shipped <= registered, shipped - registered)

    def test_ir_error_fixtures_keep_the_valid_universal_database_header(self):
        header = (ROOT / "applications/main/infrared/resources/infrared/assets/projectors.ir").read_text().splitlines()[:2]
        assets = ROOT / "applications/debug/unit_tests/resources/unit_tests/infrared"
        for name in ("empty_database.ir", "unexpected_button.ir"):
            self.assertEqual((assets / name).read_text().splitlines()[:2], header)


if __name__ == "__main__":
    unittest.main()
