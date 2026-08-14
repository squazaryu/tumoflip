#!/usr/bin/env python3
"""Source contracts for the selected NFC follow-up from Unleashed 090."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


class NfcUnleashed090FollowupTest(unittest.TestCase):
    def test_scanner_copies_protocol_elements_not_raw_bytes(self) -> None:
        scanner = source("lib/nfc/nfc_scanner.c")
        self.assertRegex(
            scanner,
            re.compile(
                r"memcpy\(\s*instance->detected_protocols,\s*"
                r"filtered_protocols,\s*filtered_protocols_num\s*\*\s*"
                r"sizeof\(NfcProtocol\)\s*\);",
                re.MULTILINE,
            ),
        )
        self.assertNotIn(
            "memcpy(instance->detected_protocols, filtered_protocols, "
            "filtered_protocols_num);",
            scanner,
        )

    def test_ndef_accepts_newly_classified_tag_families(self) -> None:
        ndef = source("applications/main/nfc/plugins/supported_cards/ndef.c")
        self.assertIn("MfUltralightTypeNTAG210", ndef)
        self.assertIn("MfUltralightTypeNTAG212", ndef)
        self.assertIn("MfClassicType2k", ndef)

    def test_mifare_plus_get_version_accepts_valid_nxp_plus_reply(self) -> None:
        poller = source("lib/nfc/protocols/mf_plus/mf_plus_poller_i.c")
        self.assertIn("mf_plus_version_parse(data, instance->result_buffer)", poller)
        self.assertIn("data->hw_vendor == 0x04", poller)
        self.assertIn("(data->hw_type & 0x0F) == 0x02", poller)

    def test_mifare_plus_helpers_are_defined_once(self) -> None:
        mf_plus = source("lib/nfc/protocols/mf_plus/mf_plus_i.c")
        mf_plus_poller_header = source(
            "lib/nfc/protocols/mf_plus/mf_plus_poller_i.h"
        )
        self.assertEqual(
            mf_plus.count(
                "static MfPlusType mf_plus_type_from_ats("
                "const uint8_t* historical_bytes, size_t len)"
            ),
            1,
        )
        self.assertEqual(
            mf_plus.count(
                "static MfPlusSize mf_plus_size_from_atqa(const uint8_t atqa[2])"
            ),
            1,
        )
        self.assertNotIn("mf_plus_ats_t1_tk_values", mf_plus)
        self.assertEqual(mf_plus_poller_header.count("} MfPlusProbeResult;"), 1)

    def test_ultralight_aes_uses_full_protocol_emulation(self) -> None:
        support = source(
            "applications/main/nfc/helpers/protocol_support/mf_ultralight/"
            "mf_ultralight.c"
        )
        render = source(
            "applications/main/nfc/helpers/protocol_support/mf_ultralight/"
            "mf_ultralight_render.c"
        )
        self.assertIn("MfUltralightTypeUltralightAES", support)
        self.assertIn("NfcProtocolFeatureEmulateFull", support)
        self.assertIn(
            "nfc_listener_alloc(instance->nfc, NfcProtocolMfUltralight, data)",
            support,
        )
        self.assertNotIn("NfcProtocolFeatureEmulateUid", support)
        self.assertIn("MfUltralightTypeUltralightAES", render)


if __name__ == "__main__":
    unittest.main()
