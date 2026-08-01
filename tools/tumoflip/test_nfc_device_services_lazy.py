#!/usr/bin/env python3
"""Regression contracts for heap-isolated NFC location sidecars."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_FAM = REPO_ROOT / "applications/main/nfc/application.fam"
APP_SOURCE = REPO_ROOT / "applications/main/nfc/nfc_app.c"
APP_HEADER = REPO_ROOT / "applications/main/nfc/nfc_app_i.h"
PLUGIN_SOURCE = (
    REPO_ROOT
    / "applications/main/nfc/plugins/location_sidecar/nfc_location_sidecar_plugin.c"
)
READ_SOURCE = REPO_ROOT / "applications/main/nfc/scenes/nfc_scene_read.c"
DETECT_SOURCE = REPO_ROOT / "applications/main/nfc/scenes/nfc_scene_detect.c"


def function_body(source: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        source,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class NfcDeviceServicesLazyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app_fam = APP_FAM.read_text(encoding="utf-8")
        cls.app_source = APP_SOURCE.read_text(encoding="utf-8")
        cls.app_header = APP_HEADER.read_text(encoding="utf-8")
        cls.plugin_source = PLUGIN_SOURCE.read_text(encoding="utf-8")
        cls.read_source = READ_SOURCE.read_text(encoding="utf-8")
        cls.detect_source = DETECT_SOURCE.read_text(encoding="utf-8")

    def test_main_nfc_fap_does_not_link_device_services(self) -> None:
        main_app = self.app_fam.split("# Protocol support plugins", maxsplit=1)[0]
        first_app, sidecar_app = main_app.split("\n\nApp(", maxsplit=1)
        self.assertNotIn("tumoflip_device_services", first_app)
        self.assertIn('appid="nfc_location_sidecar"', sidecar_app)
        self.assertIn('fap_libs=["tumoflip_device_services"]', sidecar_app)
        self.assertIn("fal_embedded=True", sidecar_app)

    def test_main_app_is_zero_initialized_without_direct_service_client(self) -> None:
        alloc = function_body(self.app_source, "nfc_app_alloc(void)")
        self.assertIn("memset(instance, 0, sizeof(NfcApp));", alloc)
        self.assertNotIn("tumoflip_device_services", self.app_source)
        self.assertNotIn("TumoflipDeviceServicesClient", self.app_header)
        self.assertNotIn("sidecar_path", self.app_header)

    def test_sidecar_plugin_owns_the_heavy_client_and_transactional_write(self) -> None:
        for symbol in (
            "tumoflip_device_services_client_alloc",
            "tumoflip_device_services_client_request_location",
            "tumoflip_device_services_write_sidecar",
            "tumoflip_device_services_client_cancel",
            "tumoflip_device_services_client_free",
        ):
            self.assertIn(symbol, self.plugin_source)

    def test_sidecar_is_loaded_only_after_a_save_request(self) -> None:
        request = function_body(self.app_source, "nfc_request_location_sidecar(")
        protocol_release = request.index("nfc_protocol_support_free(nfc);")
        plugin_load = request.index("plugin_manager_alloc")
        self.assertLess(protocol_release, plugin_load)
        self.assertIn("plugin_manager_alloc", request)
        self.assertIn("plugin_manager_load_single", request)
        self.assertIn("location_sidecar_plugin->start", request)

        release = function_body(
            self.app_source, "void nfc_release_location_sidecar("
        )
        self.assertIn("location_sidecar_plugin->stop", release)
        self.assertIn("plugin_manager_free", release)
        self.assertIn("location_sidecar_plugin_manager = NULL;", release)

    def test_read_detect_and_file_load_release_sidecar_before_nfc_work(self) -> None:
        for source, signature, marker in (
            (self.read_source, "nfc_scene_read_on_enter(", "nfc_supported_cards_load_cache"),
            (
                self.detect_source,
                "nfc_scene_detect_on_enter(",
                "nfc_supported_cards_load_cache",
            ),
            (self.app_source, "bool nfc_load_file(", "nfc_device_load"),
        ):
            body = function_body(source, signature)
            release = body.index("nfc_release_location_sidecar(instance);")
            self.assertLess(release, body.index(marker))


if __name__ == "__main__":
    unittest.main()
