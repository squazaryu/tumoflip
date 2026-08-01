#!/usr/bin/env python3
"""Regression contracts for lazy NFC location-sidecar services."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_SOURCE = REPO_ROOT / "applications/main/nfc/nfc_app.c"
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
        cls.app_source = APP_SOURCE.read_text(encoding="utf-8")
        cls.read_source = READ_SOURCE.read_text(encoding="utf-8")
        cls.detect_source = DETECT_SOURCE.read_text(encoding="utf-8")

    def test_app_is_zero_initialized_without_eager_location_client(self) -> None:
        alloc = function_body(self.app_source, "nfc_app_alloc(void)")
        self.assertIn("memset(instance, 0, sizeof(NfcApp));", alloc)
        self.assertNotIn("tumoflip_device_services_client_alloc", alloc)

    def test_sidecar_request_allocates_client_only_when_needed(self) -> None:
        request = function_body(self.app_source, "nfc_request_location_sidecar(")
        self.assertIn("if(!nfc->device_services)", request)
        self.assertIn("tumoflip_device_services_client_alloc", request)
        self.assertIn("tumoflip_device_services_client_request_location", request)
        self.assertIn("nfc_release_location_sidecar_client(nfc);", request)

    def test_release_cancels_frees_and_clears_client(self) -> None:
        release = function_body(
            self.app_source, "void nfc_release_location_sidecar_client("
        )
        self.assertIn("tumoflip_device_services_client_cancel", release)
        self.assertIn("tumoflip_device_services_client_free", release)
        self.assertIn("nfc->device_services = NULL;", release)

        app_free = function_body(self.app_source, "void nfc_app_free(")
        self.assertIn("nfc_release_location_sidecar_client(instance);", app_free)

    def test_read_and_detect_release_optional_client_before_nfc_work(self) -> None:
        for source, signature in (
            (self.read_source, "nfc_scene_read_on_enter("),
            (self.detect_source, "nfc_scene_detect_on_enter("),
        ):
            body = function_body(source, signature)
            release = body.index("nfc_release_location_sidecar_client(instance);")
            cache = body.index("nfc_supported_cards_load_cache")
            self.assertLess(release, cache)

    def test_saved_file_load_releases_optional_client_before_parsing(self) -> None:
        load_file = function_body(self.app_source, "bool nfc_load_file(")
        release = load_file.index("nfc_release_location_sidecar_client(instance);")
        load = load_file.index("nfc_device_load")
        self.assertLess(release, load)


if __name__ == "__main__":
    unittest.main()
