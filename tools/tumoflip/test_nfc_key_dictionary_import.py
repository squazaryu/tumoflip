#!/usr/bin/env python3
"""Regression contracts for transactional MIFARE Classic key import."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def function_body(contents: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        contents,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class NfcKeyDictionaryImportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = source("applications/main/nfc/helpers/nfc_key_dict.h")
        cls.helper = source("applications/main/nfc/helpers/nfc_key_dict.c")
        cls.importer = source("applications/main/nfc/helpers/nfc_key_dict_import.c")
        cls.classic = source(
            "applications/main/nfc/helpers/protocol_support/mf_classic/mf_classic.c"
        )
        cls.scene_config = source(
            "applications/main/nfc/scenes/nfc_scene_config.h"
        )
        cls.keys_header = source("lib/toolbox/keys_dict.h")
        cls.keys_source = source("lib/toolbox/keys_dict.c")

    def test_mifare_menus_offer_import_only_when_a_key_was_recovered(self) -> None:
        self.assertIn("SubmenuIndexSaveKeys", self.classic)
        self.assertIn("(data->key_a_mask | data->key_b_mask) == 0", self.classic)
        self.assertIn('"Save Keys to Dictionary"', self.classic)
        self.assertEqual(
            self.classic.count(
                "nfc_scene_menu_add_save_keys_mf_classic(instance, data);"
            ),
            2,
        )
        self.assertIn("NfcSceneKeyDictImport", self.classic)

    def test_collector_uses_found_masks_and_deduplicates_both_key_types(self) -> None:
        self.assertIn("NFC_KEY_DICT_DEVICE_KEYS_MAX (80)", self.header)
        collector = function_body(self.helper, "nfc_key_dict_collect_mf_classic(")
        self.assertIn("FURI_BIT(data->key_a_mask, sector)", collector)
        self.assertIn("FURI_BIT(data->key_b_mask, sector)", collector)
        self.assertEqual(collector.count("nfc_key_dict_push_unique("), 2)

    def test_dictionary_scans_are_explicitly_read_only(self) -> None:
        scan = function_body(self.importer, "nfc_key_dict_mark_path_present(")
        self.assertIn("file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)", scan)
        self.assertIn("file_stream_get_error(stream) == FSE_OK", scan)
        self.assertNotIn("keys_dict_alloc(", self.importer)

    def test_missing_system_dictionary_fails_before_user_file_access(self) -> None:
        importer = function_body(self.importer, "void nfc_key_dict_import(")
        missing = importer.index("NfcKeyDictImportStatusSystemDictionaryMissing")
        user_scan = importer.index("dict->user_path")
        transaction = importer.index("nfc_key_dict_append_transaction(")

        self.assertLess(missing, user_scan)
        self.assertLess(missing, transaction)

    def test_transaction_backs_up_before_the_first_write(self) -> None:
        transaction = function_body(
            self.importer, "nfc_key_dict_append_transaction("
        )
        backup = transaction.index("nfc_key_dict_create_backup(")
        write = transaction.index("storage_file_write(")
        self.assertLess(backup, write)

    def test_success_is_reported_only_after_write_and_sync(self) -> None:
        transaction = function_body(
            self.importer, "nfc_key_dict_append_transaction("
        )
        write = transaction.index("storage_file_write(")
        sync = transaction.index("storage_file_sync(", write)
        report = transaction.index("stats->added = keys_to_add", sync)

        self.assertLess(write, sync)
        self.assertLess(sync, report)

    def test_failed_write_restores_original_or_preserves_backup(self) -> None:
        transaction = function_body(
            self.importer, "nfc_key_dict_append_transaction("
        )
        write = transaction.index("storage_file_write(")
        rollback = transaction.index("nfc_key_dict_restore_original(", write)
        rollback_failed = transaction.index(
            "NfcKeyDictImportStatusRollbackFailed", rollback
        )

        self.assertLess(write, rollback)
        self.assertLess(rollback, rollback_failed)

    def test_shared_keys_dict_keeps_existing_write_semantics(self) -> None:
        add_key = function_body(self.keys_source, "static bool keys_dict_add_key_str(")
        self.assertIn("stream_insert_string(instance->stream, key)", add_key)
        self.assertNotIn("stream_write_string(instance->stream, key)", add_key)

    def test_scene_reports_fail_closed_outcomes(self) -> None:
        scene = source("applications/main/nfc/scenes/nfc_scene_key_dict_import.c")
        self.assertIn("NfcKeyDictImportStatusSystemDictionaryMissing", scene)
        self.assertIn("System dictionary", scene)
        self.assertIn("Dictionary unchanged", scene)
        self.assertIn("Backup preserved", scene)
        self.assertIn("ADD_SCENE(nfc, key_dict_import, KeyDictImport)", self.scene_config)


if __name__ == "__main__":
    unittest.main()
