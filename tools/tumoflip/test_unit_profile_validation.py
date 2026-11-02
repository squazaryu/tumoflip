"""Fail closed on diagnostic FAL imports, plugin sets and profile identity."""

from pathlib import Path
import unittest

try:
    from tools.tumoflip import validate_unit_profile as validator
except ImportError:
    validator = None

ROOT = Path(__file__).resolve().parents[2]


class UnitProfileValidationTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(validator, "the separate profile needs an artifact/import validator")

    def test_matching_host_and_firmware_exports_resolve(self):
        result = validator.check_host_imports({"fw"}, {"host"}, {"host"},
                                             {"test_one.fal": {"host", "fw"}})
        self.assertEqual(result["test_one.fal"], 2)

    def test_private_export_must_really_exist_in_resident_elf(self):
        with self.assertRaisesRegex(ValueError, "host"):
            validator.check_host_imports({"fw"}, {"host"}, set(), {"test.fal": {"fw"}})

    def test_arbitrary_resident_symbol_is_not_implicitly_exported(self):
        with self.assertRaisesRegex(ValueError, "hidden"):
            validator.check_host_imports(set(), set(), {"hidden"}, {"test.fal": {"hidden"}})

    def test_excluded_js_cannot_be_borrowed_from_another_fap(self):
        with self.assertRaisesRegex(ValueError, "js_thread_run"):
            validator.check_host_imports(set(), set(), set(), {"test.fal": {"js_thread_run"}})

    def test_plugin_set_requires_all_and_only_declared_tests(self):
        validator.check_plugin_set({"test_one.fal"}, {"test_one.fal"})
        for actual in (set(), {"test_js.fal"}, {"test_one.fal", "test_js.fal"}):
            with self.assertRaises(ValueError):
                validator.check_plugin_set({"test_one.fal"}, actual)

    def test_declarations_are_read_from_manifest_without_running_it(self):
        result = validator.declared_plugins('App(appid="test_a")\nApp(appid="unit_tests")')
        self.assertEqual(result, {"test_a.fal"})
        with self.assertRaises(ValueError):
            validator.declared_plugins('App(appid="test_js")')

    def test_private_algorithm_implementations_stay_in_test_plugins(self):
        manifest = (ROOT / "applications/debug/unit_tests/application.fam").read_text()
        self.assertIn('"../../lib/subghz/protocols/kia_v1.c"', manifest)
        self.assertIn('"../../lib/nfc/protocols/mf_ultralight/mf_ultralight_aes_crypto.c"', manifest)
        api = (ROOT / "targets/f7/api_symbols.csv").read_text()
        self.assertNotIn("Function,+,mf_ultralight_aes_cmac,", api)
        self.assertNotIn("Function,+,subghz_protocol_decoder_kia_v1_alloc,", api)

    def test_ci_builds_and_validates_the_unit_profile_separately(self):
        workflow = (ROOT / ".github/workflows/pr-build.yml").read_text()
        self.assertIn("unit-tests-profile:", workflow)
        self.assertIn("FIRMWARE_APP_SET=unit_tests", workflow)
        self.assertIn("validate_unit_profile.py", workflow)


if __name__ == "__main__":
    unittest.main()
