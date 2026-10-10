#!/usr/bin/env python3
"""Source contracts for the NFC Texkom reader, emulator, and manual generator."""

from pathlib import Path
import csv
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


class TexkomSupportTest(unittest.TestCase):
    def test_protocol_is_registered_for_read_and_emulation(self) -> None:
        protocol = source("lib/nfc/protocols/nfc_protocol.h")
        self.assertTrue(
            protocol.index("NfcProtocolTexkom") > protocol.index("NfcProtocolEmv"),
            "append Texkom after existing public NFC protocol IDs",
        )
        for path, entry in (
            ("lib/nfc/protocols/nfc_device_defs.c", "nfc_device_texkom"),
            ("lib/nfc/protocols/nfc_poller_defs.c", "nfc_poller_texkom"),
            ("lib/nfc/protocols/nfc_listener_defs.c", "nfc_listener_texkom"),
            ("applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c", '"texkom"'),
        ):
            self.assertIn(entry, source(path), f"missing Texkom registry entry in {path}")

    def test_supported_variants_and_no_fake_tk15_generator(self) -> None:
        generator_header = source("lib/nfc/helpers/nfc_data_generator.h")
        protocol = source("lib/nfc/protocols/texkom/texkom.h")
        generator = source("lib/nfc/helpers/nfc_data_generator.c")
        self.assertTrue(
            generator_header.index("NfcDataGeneratorTypeTexkomTk13")
            > generator_header.index("NfcDataGeneratorTypeMfUltralightAES"),
            "append Texkom generators without renumbering existing public values",
        )
        for variant in (
            "TexkomTypeTk13",
            "TexkomTypeTk15",
            "TexkomTypeTk17",
            "TexkomTypeMmbit",
        ):
            self.assertIn(variant, protocol)
        for generator_name in ("Texkom TK13", "Texkom TK17", "Texkom MMBIT"):
            self.assertIn(generator_name, generator)
        self.assertNotIn("Texkom TK15", generator)

    def test_cli_emulation_and_generic_generate_info_are_wired(self) -> None:
        emulate = source("applications/main/nfc/cli/commands/nfc_cli_command_emulate.c")
        generate_info = source("applications/main/nfc/scenes/nfc_scene_generate_info.c")
        self.assertIn("NfcProtocolTexkom", emulate)
        self.assertIn("nfc_device_get_protocol_name(root)", generate_info)
        self.assertIn("nfc_device_get_uid(instance->nfc_device", generate_info)

    def test_public_api_matches_texkom_fap_contract(self) -> None:
        with (REPO_ROOT / "targets/f7/api_symbols.csv").open(encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))

        version = next(row["name"] for row in rows if row["entry"] == "Version")
        self.assertEqual(version, "88.17")
        public_symbols = {
            row["name"] for row in rows if row["entry"] in {"Header", "Function"}
        }
        self.assertIn("lib/nfc/protocols/texkom/texkom.h", public_symbols)
        self.assertIn("nfc_texkom_poller_rx", public_symbols)
        unit_test_api = source("applications/debug/unit_tests/unit_test_api_table_i.h")
        self.assertIn("texkom_make_blank", unit_test_api)

    def test_decoder_storage_and_generator_regressions_are_registered(self) -> None:
        tests = source("applications/debug/unit_tests/tests/nfc/nfc_test.c")
        for test_name in (
            "texkom_decode_tk13",
            "texkom_decode_tk15",
            "texkom_decode_tk17",
            "texkom_decode_mmbit",
            "texkom_decode_rejects_junk",
            "texkom_crc_reference_values",
            "texkom_expected_crc",
            "texkom_make_blank_frames",
            "texkom_uid_accessors",
            "texkom_file_tests",
        ):
            self.assertIn(f"MU_RUN_TEST({test_name})", tests)


if __name__ == "__main__":
    unittest.main()
