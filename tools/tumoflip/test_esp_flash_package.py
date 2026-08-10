import pathlib
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/esp_flasher"


class EspFlashPackageTests(unittest.TestCase):
    def test_native_manifest_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / "package-plan-test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{APP_ROOT}",
                    f"-I{APP_ROOT / 'lib/frozen'}",
                    str(APP_ROOT / "tests/package_plan_test.c"),
                    str(APP_ROOT / "esp_flash_package_plan.c"),
                    str(APP_ROOT / "lib/frozen/frozen.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            result = subprocess.run(
                [str(binary)], check=True, capture_output=True, text=True
            )
            self.assertIn("package plan tests: OK", result.stdout)

    def test_package_mode_gates_target_and_uses_ascii_md5(self):
        worker = (APP_ROOT / "esp_flasher_worker.c").read_text(encoding="utf-8")
        target_gate = worker.index("esp_loader_get_target()")
        first_package_flash = worker.index("_flash_package_files(app)")
        self.assertLess(target_gate, first_package_flash)
        self.assertIn("uint8_t expected_md5_hex[33]", worker)
        self.assertIn("esp_loader_flash_verify_known_md5", worker)
        self.assertIn("Package flash failed. Target was not reset.", worker)

    def test_package_picker_is_manifest_only_and_manual_mode_remains(self):
        storage = (APP_ROOT / "esp_flash_package_storage.c").read_text(encoding="utf-8")
        start = (APP_ROOT / "scenes/esp_flasher_scene_start.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("ESP_FLASH_PACKAGE_MANIFEST", storage)
        self.assertIn("Extra or duplicate .bin file", storage)
        self.assertIn('"Flash Package"', start)
        self.assertIn('"Manual Flash"', start)

    def test_offline_c5_fallback_is_versioned_and_back_routes_to_c5(self):
        quick = (APP_ROOT / "scenes/esp_flasher_scene_quick.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('"Marauder 1.13.0 (offline)"', quick)
        self.assertLess(
            quick.index("else if(state > QuickC5Boot)"),
            quick.index("else if(state > QuickS3Boot)"),
        )


if __name__ == "__main__":
    unittest.main()
