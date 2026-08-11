#!/usr/bin/env python3
"""Regression contracts for save-before-delete capture replacement."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative: str) -> str:
    return (REPO_ROOT / relative).read_text(encoding="utf-8")


def function_body(contents: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        contents,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class SaveBeforeDeleteTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ibutton_save_name = source(
            "applications/main/ibutton/scenes/ibutton_scene_save_name.c"
        )
        cls.lfrfid = source("applications/main/lfrfid/lfrfid.c")
        cls.lfrfid_save_data = source(
            "applications/main/lfrfid/scenes/lfrfid_scene_save_data.c"
        )
        cls.lfrfid_save_name = source(
            "applications/main/lfrfid/scenes/lfrfid_scene_save_name.c"
        )
        cls.nfc = source("applications/main/nfc/nfc_app.c")
        cls.nfc_save_name = source(
            "applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c"
        )

    def test_ibutton_replacement_is_saved_before_old_file_is_removed(self) -> None:
        handler = function_body(
            self.ibutton_save_name, "bool ibutton_scene_save_name_on_event("
        )
        save = handler.index("const bool saved = ibutton_save_key(ibutton);")
        remove = handler.index("storage_simply_remove", save)
        self.assertLess(save, remove)
        self.assertIn("furi_string_set(ibutton->file_path, replaced_path);", handler)

    def test_lfrfid_replacement_is_saved_before_old_file_is_removed(self) -> None:
        save_data = function_body(
            self.lfrfid_save_data, "bool lfrfid_scene_save_data_on_event("
        )
        self.assertNotIn("lfrfid_delete_key", save_data)
        self.assertIn("if(lfrfid_save_key(app))", save_data)

        save_name = function_body(
            self.lfrfid_save_name, "bool lfrfid_scene_save_name_on_event("
        )
        save = save_name.index("const bool saved = lfrfid_save_key(app);")
        reconcile = save_name.index("lfrfid_reconcile_replaced_key", save)
        remove = save_name.index("lfrfid_delete_key_file", reconcile)
        self.assertLess(save, reconcile)
        self.assertLess(reconcile, remove)
        self.assertIn("furi_string_set(app->file_path, replaced_path);", save_name)
        self.assertIn("furi_string_set(app->file_name, replaced_name);", save_name)

    def test_nfc_replacement_is_saved_before_old_file_is_removed(self) -> None:
        save_file = function_body(self.nfc, "bool nfc_save_file(")
        self.assertIn("nfc_device_save(instance->nfc_device, furi_string_get_cstr(path))", save_file)
        self.assertNotIn("furi_string_get_cstr(instance->file_path)", save_file)

        handler = function_body(
            self.nfc_save_name,
            "nfc_protocol_support_scene_save_name_on_event(",
        )
        save = handler.index("const bool saved = nfc_save(instance);")
        reconcile = handler.index("nfc_reconcile_replaced_file", save)
        remove = handler.index("nfc_delete_file", reconcile)
        self.assertLess(save, reconcile)
        self.assertLess(reconcile, remove)
        self.assertIn("furi_string_set(instance->file_path, replaced_path);", handler)
        self.assertIn("furi_string_set(instance->file_name, replaced_name);", handler)

    def test_location_sidecars_follow_safe_rename_and_delete(self) -> None:
        for contents, reconcile_signature, delete_signature in (
            (
                self.lfrfid,
                "static bool lfrfid_reconcile_location_sidecar(",
                "bool lfrfid_delete_key_file(",
            ),
            (
                self.nfc,
                "bool nfc_reconcile_replaced_file(",
                "bool nfc_delete_file(",
            ),
        ):
            reconcile = function_body(contents, reconcile_signature)
            self.assertIn('".tumoflip.json"', contents)
            self.assertIn("storage_common_copy", reconcile)
            self.assertIn("storage_simply_remove", reconcile)
            self.assertIn("storage_file_exists", reconcile)

            delete = function_body(contents, delete_signature)
            self.assertIn("sidecar", delete)
            self.assertIn("storage_simply_remove", delete)


if __name__ == "__main__":
    unittest.main()
