#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/tumotag_verify"
SOURCE = (APP_DIR / "tumotag_verify.c").read_text(encoding="utf-8")
COMPARE = (APP_DIR / "tumotag_compare.c").read_text(encoding="utf-8")
STORAGE = (APP_DIR / "tumotag_storage.c").read_text(encoding="utf-8")
STORAGE_HEADER = (APP_DIR / "tumotag_storage.h").read_text(encoding="utf-8")
MANIFEST = (APP_DIR / "application.fam").read_text(encoding="utf-8")
NFC_APP = (REPO_ROOT / "applications/main/nfc/nfc_app.c").read_text(encoding="utf-8")
NFC_SUCCESS = (
    REPO_ROOT / "applications/main/nfc/scenes/nfc_scene_read_success.c"
).read_text(encoding="utf-8")
VALIDATOR = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
    encoding="utf-8"
)


class TumoTagVerifyTest(unittest.TestCase):
    def test_external_api_88_fap_is_packaged(self) -> None:
        self.assertIn('appid="tumotag_verify"', MANIFEST)
        self.assertIn("FlipperAppType.EXTERNAL", MANIFEST)
        self.assertIn('fap_category="Module One/NFC"', MANIFEST)
        self.assertIn(
            'fap_dist_path="apps/Module One/NFC/tumotag_verify.fap"', MANIFEST
        )
        self.assertIn('"apps/Module One/NFC/tumotag_verify.fap"', VALIDATOR)

    def test_product_ui_has_progress_actions_and_about(self) -> None:
        self.assertIn('elements_button_left(canvas, "Cancel")', SOURCE)
        self.assertIn('elements_button_center(canvas, "Retry")', SOURCE)
        self.assertIn('"Find saved token"', SOURCE)
        self.assertIn('"Last result"', SOURCE)
        self.assertIn('"About"', SOURCE)
        self.assertIn("TUMOTAG_SEARCH_MAX_FILES 256U", SOURCE)
        self.assertIn("TUMOTAG_SEARCH_MAX_DEPTH 6U", SOURCE)

    def test_nfc_capture_reuses_stock_reader_and_returns_to_fap(self) -> None:
        self.assertIn("NFC_TUMOTAG_VERIFY_CAPTURE_ARG", NFC_APP)
        self.assertIn("NfcSceneDetect", NFC_APP)
        self.assertIn("NFC_TUMOTAG_VERIFY_OUTPUT", NFC_SUCCESS)
        self.assertIn("nfc_device_save", NFC_SUCCESS)
        self.assertIn("loader_enqueue_launch", SOURCE)
        self.assertIn("tumotag_route_resume", SOURCE)

    def test_comparison_never_treats_uid_only_as_verified(self) -> None:
        self.assertIn("nfc_device_is_equal", COMPARE)
        self.assertIn("mf_ultralight_is_all_data_read", COMPARE)
        self.assertIn("mf_classic_is_card_read", COMPARE)
        self.assertIn("TumoTagVerdictPartial", COMPARE)
        verified_assignment = "result->verdict = TumoTagVerdictVerified"
        self.assertEqual(COMPARE.count(verified_assignment), 3)

    def test_app_is_read_only_and_reports_atomically(self) -> None:
        combined = "\n".join((SOURCE, COMPARE, STORAGE))
        for forbidden in (
            "nfc_listener",
            "nfc_device_set_uid",
            "lfrfid_worker_write",
            "ibutton_worker_write",
            "emulate_start",
            "dictionary_attack",
        ):
            self.assertNotIn(forbidden, combined)
        self.assertIn("tumotag_replace_file", STORAGE)
        self.assertIn("route.tmp", STORAGE)
        self.assertIn("last_result.tmp", STORAGE)
        self.assertIn('EXT_PATH("apps_data/tumotag_verify")', STORAGE_HEADER)


if __name__ == "__main__":
    unittest.main()
