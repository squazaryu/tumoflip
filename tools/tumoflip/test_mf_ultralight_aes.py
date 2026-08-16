#!/usr/bin/env python3
"""Fail-closed regression contracts for MIFARE Ultralight AES support."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER = REPO_ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight.h"
DATA = REPO_ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight.c"
CRYPTO = REPO_ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight_aes_crypto.c"
POLLER = REPO_ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight_poller.c"
POLLER_INTERNAL = (
    REPO_ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight_poller_i.c"
)
LISTENER = REPO_ROOT / "lib/nfc/protocols/mf_ultralight/mf_ultralight_listener.c"
UNLOCK_WARN = (
    REPO_ROOT / "applications/main/nfc/scenes/nfc_scene_des_auth_unlock_warn.c"
)
APP_SUPPORT = (
    REPO_ROOT
    / "applications/main/nfc/helpers/protocol_support/mf_ultralight/mf_ultralight.c"
)
DICT_SCENE = (
    REPO_ROOT
    / "applications/main/nfc/helpers/protocol_support/mf_ultralight/mf_ultralight_extra_scenes.c"
)
API_SYMBOLS = REPO_ROOT / "targets/f7/api_symbols.csv"
HOST_TEST = REPO_ROOT / "tools/tumoflip/fixtures/mf_ultralight_aes_host_test.c"
GENERATOR = REPO_ROOT / "lib/nfc/helpers/nfc_data_generator.c"
GENERATOR_HEADER = REPO_ROOT / "lib/nfc/helpers/nfc_data_generator.h"


def function_body(contents: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        contents,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class MfUltralightAesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.data = DATA.read_text(encoding="utf-8")
        cls.crypto = CRYPTO.read_text(encoding="utf-8")
        cls.poller = POLLER.read_text(encoding="utf-8")
        cls.poller_internal = POLLER_INTERNAL.read_text(encoding="utf-8")
        cls.listener = LISTENER.read_text(encoding="utf-8")
        cls.unlock_warn = UNLOCK_WARN.read_text(encoding="utf-8")
        cls.app_support = APP_SUPPORT.read_text(encoding="utf-8")
        cls.dict_scene = DICT_SCENE.read_text(encoding="utf-8")
        cls.api_symbols = API_SYMBOLS.read_text(encoding="utf-8")
        cls.generator = GENERATOR.read_text(encoding="utf-8")
        cls.generator_header = GENERATOR_HEADER.read_text(encoding="utf-8")

    def test_manual_ultralight_generators_use_reviewed_factory_layouts(self) -> None:
        generator_enum = self.generator_header[
            self.generator_header.index("typedef enum {") :
            self.generator_header.index("} NfcDataGeneratorType;")
        ]
        self.assertLess(
            generator_enum.index("NfcDataGeneratorTypeMfUltralightC"),
            generator_enum.index("NfcDataGeneratorTypeMfUltralightAES"),
        )

        ulc = function_body(
            self.generator, "static void nfc_generate_mf_ultralight_c("
        )
        self.assertIn("MfUltralightTypeMfulC", ulc)
        self.assertIn("pages_total = 48", ulc)
        self.assertIn("pages_read = 48", ulc)
        self.assertIn("page[2].data[1] = 0x48", ulc)
        self.assertIn("page[0x2A].data[0] = 0x30", ulc)
        self.assertIn("mf_ultralight_c_default_key", ulc)
        self.assertIn('"BREAKMEIFYOUCAN!"', self.generator)

        aes = function_body(
            self.generator, "static void nfc_generate_mf_ultralight_aes("
        )
        self.assertIn("MfUltralightTypeUltralightAES", aes)
        self.assertIn("pages_total = 60", aes)
        self.assertIn("pages_read = 60", aes)
        self.assertIn("version_bytes_mf0aes20", aes)
        self.assertIn("MF_ULTRALIGHT_AES_CFG_PAGE", aes)
        self.assertIn(".data[3] = 0x3C", aes)
        self.assertIn("MF_ULTRALIGHT_AES_ACCESS_PROT", aes)
        self.assertIn("MF_ULTRALIGHT_AES_ACCESS_CNT_INC_EN", aes)
        self.assertIn("MF_ULTRALIGHT_AES_ACCESS_CNT_RD_EN", aes)
        self.assertIn(".data[1] = 0x05", aes)
        self.assertIn("furi_hal_random_fill_buf", aes)
        self.assertIn("aes_signature_present = true", aes)

    def test_manual_generator_ids_are_append_only(self) -> None:
        generator_enum = self.generator_header[
            self.generator_header.index("typedef enum {") :
            self.generator_header.index("} NfcDataGeneratorType;")
        ]
        names = re.findall(r"\b(NfcDataGeneratorType[A-Za-z0-9_]+)\s*,", generator_enum)
        legacy_names = [
            "NfcDataGeneratorTypeMfUltralight",
            "NfcDataGeneratorTypeMfUltralightEV1_11",
            "NfcDataGeneratorTypeMfUltralightEV1_H11",
            "NfcDataGeneratorTypeMfUltralightEV1_21",
            "NfcDataGeneratorTypeMfUltralightEV1_H21",
            "NfcDataGeneratorTypeNTAG203",
            "NfcDataGeneratorTypeNTAG213",
            "NfcDataGeneratorTypeNTAG215",
            "NfcDataGeneratorTypeNTAG216",
            "NfcDataGeneratorTypeNTAGI2C1k",
            "NfcDataGeneratorTypeNTAGI2C2k",
            "NfcDataGeneratorTypeNTAGI2CPlus1k",
            "NfcDataGeneratorTypeNTAGI2CPlus2k",
            "NfcDataGeneratorTypeMfClassicMini",
            "NfcDataGeneratorTypeMfClassic1k_4b",
            "NfcDataGeneratorTypeMfClassic1k_7b",
            "NfcDataGeneratorTypeMfClassic4k_4b",
            "NfcDataGeneratorTypeMfClassic4k_7b",
            "NfcDataGeneratorTypeMfPlusSE_4b",
            "NfcDataGeneratorTypeMfPlusSE_7b",
            "NfcDataGeneratorTypeMfPlusS2k_4b",
            "NfcDataGeneratorTypeMfPlusS2k_7b",
            "NfcDataGeneratorTypeMfPlusS4k_4b",
            "NfcDataGeneratorTypeMfPlusS4k_7b",
            "NfcDataGeneratorTypeMfPlusX2k_4b",
            "NfcDataGeneratorTypeMfPlusX2k_7b",
            "NfcDataGeneratorTypeMfPlusX4k_4b",
            "NfcDataGeneratorTypeMfPlusX4k_7b",
            "NfcDataGeneratorTypeMfPlusEV1_2k_4b",
            "NfcDataGeneratorTypeMfPlusEV1_2k_7b",
            "NfcDataGeneratorTypeMfPlusEV1_4k_4b",
            "NfcDataGeneratorTypeMfPlusEV1_4k_7b",
            "NfcDataGeneratorTypeMfPlusEV2_2k_4b",
            "NfcDataGeneratorTypeMfPlusEV2_2k_7b",
            "NfcDataGeneratorTypeMfPlusEV2_4k_4b",
            "NfcDataGeneratorTypeMfPlusEV2_4k_7b",
        ]
        self.assertEqual(names[: len(legacy_names)], legacy_names)
        self.assertEqual(
            names[len(legacy_names) :],
            [
                "NfcDataGeneratorTypeMfUltralightC",
                "NfcDataGeneratorTypeMfUltralightAES",
                "NfcDataGeneratorTypeNum",
            ],
        )

        scene = (
            REPO_ROOT / "applications/main/nfc/scenes/nfc_scene_set_type.c"
        ).read_text(encoding="utf-8")
        order = function_body(
            scene, "static NfcDataGeneratorType nfc_scene_set_type_generator_at("
        )
        self.assertIn("NfcDataGeneratorTypeMfUltralightC", order)
        self.assertIn("NfcDataGeneratorTypeMfUltralightAES", order)

    def test_public_data_layout_is_append_only_and_api_stays_88(self) -> None:
        struct = self.header[
            self.header.index("typedef struct {\n    Iso14443_3aData*") :
            self.header.index("} MfUltralightData;")
        ]
        legacy_tail = struct.index("uint32_t auth_attempts;")
        aes_signature = struct.index("bool aes_signature_present;")
        aes_key = struct.index("bool aes_data_key_present;")
        self.assertLess(legacy_tail, aes_signature)
        self.assertLess(aes_signature, aes_key)
        self.assertIn("static inline bool mf_ultralight_aes_get_key", self.header)
        self.assertNotIn("mf_ultralight_aes_get_key", self.api_symbols)
        self.assertRegex(self.api_symbols, r"(?m)^Version,\+,88\.0,,$")

    def test_recovered_key_is_explicit_metadata_not_fabricated_pages(self) -> None:
        read_success = function_body(
            self.poller, "mf_ultralight_poller_handler_read_success("
        )
        self.assertIn("aes_data_key_present = true;", read_success)
        self.assertIn("aes_data_key = instance->auth_context.aes_key;", read_success)
        self.assertNotIn("MF_ULTRALIGHT_AES_DATA_KEY_PAGE", read_success)
        self.assertNotRegex(read_success, r"page\s*\[")

        save = function_body(self.data, "bool mf_ultralight_save(")
        load = function_body(self.data, "bool mf_ultralight_load(")
        for body in (save, load):
            self.assertIn("MF_ULTRALIGHT_AES_DATA_KEY", body)
            self.assertIn("aes_data_key_present", body)
        self.assertIn("zero is a valid AES key", load)

    def test_dictionary_requires_known_unlimited_auth_limit(self) -> None:
        menu_guard = function_body(
            self.app_support, "nfc_mf_ultralight_aes_dictionary_is_safe("
        )
        scene_guard = function_body(
            self.dict_scene, "nfc_mf_ultralight_aes_dict_attack_is_safe("
        )
        for guard in (menu_guard, scene_guard):
            self.assertIn("pages_read <= MF_ULTRALIGHT_AES_ACCESS_PAGE", guard)
            self.assertIn("access[2]", guard)
            self.assertIn("access[3] & 0x03U", guard)
            self.assertIn("== 0", guard)

        read_event = function_body(
            self.app_support, "bool nfc_scene_read_on_event_mf_ultralight("
        )
        incomplete = read_event[read_event.index("NfcCustomEventPollerIncomplete") :]
        self.assertIn("data->type == MfUltralightTypeMfulC", incomplete)
        self.assertNotIn("MfUltralightTypeUltralightAES", incomplete.split("} else {", 1)[0])

        worker = function_body(
            self.dict_scene,
            "nfc_mf_ultralight_aes_dict_attack_worker_callback(",
        )
        safety = worker.index("nfc_mf_ultralight_aes_dict_attack_is_safe")
        mode = worker.index("MfUltralightPollerModeDictAttack", safety)
        self.assertLess(safety, mode)
        self.assertIn("return NfcCommandStop;", worker[safety:mode])

    def test_ordinary_read_never_guesses_an_aes_key(self) -> None:
        callback = function_body(
            self.app_support, "nfc_scene_read_poller_callback_mf_ultralight("
        )
        aes_branch = callback[
            callback.index("if(data->type == MfUltralightTypeUltralightAES)") :
            callback.index("} else if(instance->mf_ul_auth->type", callback.index("if(data->type == MfUltralightTypeUltralightAES)"))
        ]
        self.assertIn("MfUltralightAuthTypeManual", aes_branch)
        self.assertIn("skip_auth = true", aes_branch)
        self.assertNotIn("random_id", aes_branch)
        self.assertNotIn("MfUltralightAesKeyTypeUid", aes_branch)
        self.assertIn("AUTH_LIM", aes_branch)

    def test_listener_rejects_unsupported_aes_key_slots(self) -> None:
        handler = function_body(
            self.listener, "mf_ultralight_aes_authenticate_handler_p1("
        )
        slot_check = handler.index("bit_buffer_get_byte(buffer, 1)")
        key_load = handler.index("mf_ultralight_aes_get_key", slot_check)
        self.assertIn("MfUltralightAesKeyTypeData", handler[slot_check:key_load])
        self.assertLess(slot_check, key_load)

    def test_manual_aes_warning_names_auth_limit_risk(self) -> None:
        self.assertIn("Wrong AES key uses\\none AUTH_LIM try", self.unlock_warn)
        self.assertIn('message = "May lock card!"', self.unlock_warn)

    def test_aes_write_uses_one_recovered_key_and_skips_config_pages(self) -> None:
        callback = function_body(
            self.app_support, "nfc_scene_write_poller_callback_mf_ultralight("
        )
        aes_branch = callback[
            callback.index("if(is_aes) {") : callback.index("} else {", callback.index("if(is_aes) {") )
        ]
        self.assertIn("wdata->aes_data_key_present", aes_branch)
        self.assertIn("wdata->aes_data_key", aes_branch)
        self.assertNotIn("keys_dict", aes_branch)

        write_pages = function_body(
            self.poller, "mf_ultralight_poller_handler_write_pages("
        )
        jump = write_pages.index("MF_ULTRALIGHT_AES_DATA_KEY_PAGE")
        self.assertIn("end_page = 0x28", self.data)
        self.assertIn("config/lock pages 0x28-0x2F", write_pages)
        self.assertIn("aes_data_key_present", write_pages[:jump])
        self.assertNotIn("page[MF_ULTRALIGHT_AES_DATA_KEY_PAGE]", write_pages)

    def test_crypto_is_checked_constant_time_and_zeroized(self) -> None:
        self.assertIn("bool mf_ultralight_aes_cmac(", self.crypto)
        self.assertIn("bool mf_ultralight_aes_cmac8_ctr(", self.crypto)
        self.assertIn("bool mf_ultralight_aes_derive_session_key(", self.crypto)
        self.assertNotIn("memcmp(", self.crypto)
        self.assertIn("difference |= left[i] ^ right[i]", self.crypto)
        self.assertGreaterEqual(self.crypto.count("mbedtls_platform_zeroize"), 10)

        auth = function_body(
            self.poller_internal, "mf_ultralight_poller_authenticate_aes("
        )
        self.assertIn("mf_ultralight_aes_equal(rec_rnd_a", auth)
        self.assertIn("mbedtls_aes_setkey_enc", auth)
        self.assertIn("mbedtls_aes_setkey_dec", auth)
        self.assertIn("mbedtls_platform_zeroize", auth)

        listener_aes = self.listener[self.listener.index("CMD_ULAES_AUTH_2") :]
        self.assertIn("mf_ultralight_aes_equal", listener_aes)
        self.assertIn("mbedtls_platform_zeroize", listener_aes)

    def test_listener_masks_all_aes_key_pages(self) -> None:
        read = function_body(
            self.listener, "mf_ultralight_listener_perform_read("
        )
        self.assertIn("page >= 0x30 && page <= 0x37", read)
        self.assertIn("never leak them", read)

    def test_secret_key_material_is_not_logged(self) -> None:
        sources = self.poller + self.poller_internal + self.listener
        aes_log_lines = [
            line
            for line in sources.splitlines()
            if "FURI_LOG" in line and ("AES" in line or "key" in line.lower())
        ]
        for line in aes_log_lines:
            self.assertNotRegex(line, r"%0?2[xX]|%s.*key")

    @unittest.skipUnless(shutil.which("cc"), "host C compiler is unavailable")
    def test_crypto_known_answers_execute_against_c_implementation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="tumoflip-ulaes-") as temp_dir:
            executable = Path(temp_dir) / "mf_ultralight_aes_host_test"
            command = [
                shutil.which("cc"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{REPO_ROOT / 'lib/mbedtls/include'}",
                f"-I{REPO_ROOT / 'lib/nfc/protocols/mf_ultralight'}",
                str(HOST_TEST),
                str(CRYPTO),
                str(REPO_ROOT / "lib/mbedtls/library/aes.c"),
                str(REPO_ROOT / "lib/mbedtls/library/aesce.c"),
                str(REPO_ROOT / "lib/mbedtls/library/aesni.c"),
                str(REPO_ROOT / "lib/mbedtls/library/platform_util.c"),
                "-o",
                str(executable),
            ]
            subprocess.run(command, check=True, cwd=REPO_ROOT)
            subprocess.run([str(executable)], check=True, cwd=REPO_ROOT)


if __name__ == "__main__":
    unittest.main()
