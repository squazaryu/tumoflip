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
                    "-include",
                    str(APP_ROOT / "tests/host_string_compat.h"),
                    f"-I{APP_ROOT}",
                    f"-I{APP_ROOT / 'lib/frozen'}",
                    str(APP_ROOT / "tests/package_plan_test.c"),
                    str(APP_ROOT / "tests/host_string_compat.c"),
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
        storage = (APP_ROOT / "esp_flash_package_storage.c").read_text(
            encoding="utf-8"
        )
        target_gate = worker.index("esp_loader_get_target()")
        first_package_flash = worker.index("_flash_package_files(app)")
        self.assertLess(target_gate, first_package_flash)
        self.assertIn("uint8_t expected_md5_hex[33]", worker)
        self.assertIn("esp_loader_flash_verify_known_md5", worker)
        self.assertIn("Package flash failed. Target was not reset.", worker)

        # The worker revalidates the complete package before erase. Keep its large plan and
        # digest scratch storage off the nested worker stack to avoid MPU stack-overflow faults.
        self.assertIn(
            "EspFlashPackagePlan* verified_plan = malloc(sizeof(*verified_plan))",
            worker,
        )
        self.assertNotIn("EspFlashPackagePlan verified_plan;", worker)
        self.assertIn("furi_thread_set_stack_size(app->flash_worker, 6 * 1024)", worker)
        self.assertIn("uint8_t* buffer = malloc(hash_buffer_size)", storage)
        self.assertNotIn("uint8_t buffer[1024]", storage)

    def test_package_picker_is_manifest_only_and_manual_mode_remains(self):
        storage = (APP_ROOT / "esp_flash_package_storage.c").read_text(encoding="utf-8")
        start = (APP_ROOT / "scenes/esp_flasher_scene_start.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("ESP_FLASH_PACKAGE_MANIFEST", storage)
        self.assertIn("Extra or duplicate .bin file", storage)
        self.assertIn('"Flash Package"', start)
        self.assertIn('"Manual Flash"', start)

    def test_packages_use_conservative_verified_transport(self):
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")
        confirm = (
            APP_ROOT / "scenes/esp_flasher_scene_package_confirm.c"
        ).read_text(encoding="utf-8")
        manual = (APP_ROOT / "scenes/esp_flasher_scene_browse.c").read_text(
            encoding="utf-8"
        )
        quick = (APP_ROOT / "scenes/esp_flasher_scene_quick.c").read_text(
            encoding="utf-8"
        )
        worker = (APP_ROOT / "esp_flasher_worker.c").read_text(encoding="utf-8")
        loader = (
            APP_ROOT / "lib/esp-serial-flasher/src/esp_loader.c"
        ).read_text(encoding="utf-8")

        self.assertIn("app->turbospeed = false;", confirm)
        self.assertNotIn(
            "app->package_plan.target != EspFlashPackageTargetEsp32C5",
            confirm,
        )
        self.assertIn("Package MD5 timed out; retrying verification once", worker)
        self.assertIn("err == ESP_LOADER_ERROR_TIMEOUT && app->package_mode", worker)
        self.assertEqual(worker.count("esp_loader_flash_verify_known_md5("), 2)
        self.assertIn("Segment %s failed with error %u", worker)
        self.assertIn("ESP32_ROM_MD5_MIN_TIMEOUT 10000", loader)
        self.assertIn("s_target == ESP32_CHIP || s_target == ESP32C5_CHIP", loader)
        self.assertIn("ESP32C5_DEFAULT_FLASH_SIZE 4 * 1024 * 1024", loader)
        self.assertIn("loader_port_start_timer(md5_timeout(size))", loader)
        self.assertIn(
            "app->turbospeed = (index == SubmenuIndexFlashTurbo)", manual
        )
        self.assertIn("app->turbospeed = true;", quick)
        self.assertIn("fap_version=(1, 13)", manifest)

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
