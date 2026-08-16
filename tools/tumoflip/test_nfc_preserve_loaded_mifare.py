#!/usr/bin/env python3
"""Regression contracts for preserving loaded MIFARE data during re-read flows."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
CLASSIC_DICT = (
    REPO_ROOT
    / "applications/main/nfc/helpers/protocol_support/mf_classic/mf_classic_extra_scenes.c"
)
CLASSIC_UPDATE = (
    REPO_ROOT
    / "applications/main/nfc/helpers/protocol_support/mf_classic/mf_classic_extra_scenes.c"
)
PLUS_DICT = (
    REPO_ROOT
    / "applications/main/nfc/helpers/protocol_support/mf_plus/mf_plus_extra_scenes.c"
)
APP_HEADER = REPO_ROOT / "applications/main/nfc/nfc_app_i.h"
POLLER_HEADER = REPO_ROOT / "lib/nfc/nfc_poller.h"


def function_body(contents: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        contents,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class NfcPreserveLoadedMifareTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.classic_dict = CLASSIC_DICT.read_text(encoding="utf-8")
        cls.classic_update = CLASSIC_UPDATE.read_text(encoding="utf-8")
        cls.plus_dict = PLUS_DICT.read_text(encoding="utf-8")
        cls.app_header = APP_HEADER.read_text(encoding="utf-8")
        cls.poller_header = POLLER_HEADER.read_text(encoding="utf-8")

    def test_classic_skip_cannot_adopt_a_pre_activation_blank(self) -> None:
        self.assertIn("bool poller_has_card_data;", self.app_header)

        callback = function_body(self.classic_dict, "nfc_dict_attack_worker_callback(")
        request_mode = callback.index("MfClassicPollerEventTypeRequestMode")
        latch = callback.index("poller_has_card_data = true;", request_mode)
        seed = callback.index("poller_mode.data = mfc_data;", request_mode)
        self.assertLess(seed, latch)

        start = function_body(
            self.classic_dict, "mf_classic_scene_dict_attack_start_poller("
        )
        self.assertIn("poller_has_card_data = false;", start)

        handler = function_body(
            self.classic_dict, "mf_classic_scene_dict_attack_on_event("
        )
        skip = handler.index("NfcCustomEventDictAttackSkip")
        guard = handler.index("if(instance->nfc_dict_context.poller_has_card_data)", skip)
        adopt = handler.index("nfc_device_set_data", guard)
        keep = handler.index("keeping the loaded card", adopt)
        self.assertLess(guard, adopt)
        self.assertLess(adopt, keep)

    def test_classic_results_and_refresh_overlay_the_loaded_dump(self) -> None:
        notify = function_body(
            self.classic_dict, "mf_classic_scene_dict_attack_notify_read("
        )
        self.assertIn("nfc_device_get_data", notify)
        self.assertNotIn("nfc_poller_get_data", notify)

        merge = function_body(
            self.classic_update,
            "mf_classic_scene_update_initial_merge(",
        )
        for required in (
            "mf_classic_is_block_read(fresh",
            "mf_classic_set_block_read(base",
            "mf_classic_is_key_found(fresh",
            "mf_classic_set_key_found(",
            "MF_CLASSIC_TOTAL_BLOCKS_MAX",
            "MF_CLASSIC_TOTAL_SECTORS_MAX",
        ):
            self.assertIn(required, merge)

        callback = function_body(
            self.classic_update,
            "nfc_mf_classic_update_initial_worker_callback(",
        )
        copy = callback.index("nfc_device_copy_data")
        overlay = callback.index("mf_classic_scene_update_initial_merge", copy)
        commit = callback.index("nfc_device_set_data", overlay)
        self.assertLess(copy, overlay)
        self.assertLess(overlay, commit)

    def test_plus_dictionary_finish_merges_instead_of_replacing(self) -> None:
        finish = function_body(
            self.plus_dict, "mf_plus_scene_dict_attack_finish("
        )
        copy = finish.index("nfc_device_copy_data")
        overlay = finish.index("mf_plus_merge_update", copy)
        commit = finish.index("nfc_device_set_data", overlay)
        self.assertLess(copy, overlay)
        self.assertLess(overlay, commit)
        self.assertNotIn(
            "nfc_device_set_data(instance->nfc_device, NfcProtocolMfPlus, data)",
            finish,
        )

    def test_poller_contract_warns_callers_about_blank_pre_activation_data(self) -> None:
        self.assertIn("before that it is a blank card", self.poller_header)
        self.assertIn("must establish that themselves", self.poller_header)


if __name__ == "__main__":
    unittest.main()
