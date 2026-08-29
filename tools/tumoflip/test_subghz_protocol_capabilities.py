#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
BROKER_HEADER = (
    REPO_ROOT
    / "applications/services/subghz_radio_broker/subghz_radio_broker.h"
)
STANDARD_TXRX = REPO_ROOT / "applications/main/subghz/helpers/subghz_txrx.c"
STANDARD_PROTOCOL_LIST = (
    REPO_ROOT / "applications/main/subghz/scenes/subghz_scene_protocol_list.c"
)
ARF_TXRX = REPO_ROOT / "applications_user/arf_subghz_full/helpers/subghz_txrx.c"
ARF_STATUS = REPO_ROOT / "applications_user/arf_tools/arf_tools.c"
ARF_STATUS_MANIFEST = REPO_ROOT / "applications_user/arf_tools/application.fam"


class SubGhzProtocolCapabilitiesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.broker_header = BROKER_HEADER.read_text(encoding="utf-8")
        cls.standard_txrx = STANDARD_TXRX.read_text(encoding="utf-8")
        cls.standard_protocol_list = STANDARD_PROTOCOL_LIST.read_text(encoding="utf-8")
        cls.arf_txrx = ARF_TXRX.read_text(encoding="utf-8")
        cls.arf_status = ARF_STATUS.read_text(encoding="utf-8")
        cls.arf_status_manifest = ARF_STATUS_MANIFEST.read_text(encoding="utf-8")

    def test_descriptor_is_versioned_compact_and_derived_from_existing_flags(self) -> None:
        for required in (
            "SUBGHZ_RADIO_BROKER_CAPABILITY_SCHEMA_VERSION 1U",
            "SubGhzRadioBrokerProtocolCapability",
            "SubGhzProtocolFlag_315",
            "SubGhzProtocolFlag_433",
            "SubGhzProtocolFlag_868",
            "SubGhzProtocolFlag_AM",
            "SubGhzProtocolFlag_FM",
            "SubGhzProtocolFlag_Decodable",
            "SubGhzProtocolFlag_Send",
            "protocol->decoder",
            "protocol->encoder",
        ):
            self.assertIn(required, self.broker_header)

        self.assertNotIn("malloc(", self.broker_header)
        self.assertNotIn("Holtek_HT12X", self.broker_header)
        self.assertNotIn("Cham_Code", self.broker_header)

    def test_validation_is_fail_closed_with_concrete_reasons(self) -> None:
        for required in (
            "SubGhzRadioBrokerValidationInvalidProtocol",
            "SubGhzRadioBrokerValidationInvalidFrequency",
            "SubGhzRadioBrokerValidationUnsupportedBand",
            "SubGhzRadioBrokerValidationUnsupportedPreset",
            "SubGhzRadioBrokerValidationUnsupportedDevice",
            "SubGhzRadioBrokerValidationReceiveUnsupported",
            "SubGhzRadioBrokerValidationTransmitUnsupported",
            "furi_hal_subghz_is_frequency_valid(frequency)",
            "subghz_radio_broker_validate_protocol",
        ):
            self.assertIn(required, self.broker_header)

    def test_standard_protocol_list_filters_incompatible_receive_paths(self) -> None:
        for required in (
            "subghz_scene_protocol_list_validate",
            "SubGhzRadioBrokerValidationOk",
            "SubGhzProtocolFlag_Decodable",
            "SubGhzProtocolFlag_315",
            "SubGhzProtocolFlag_AM",
            "SubGhzProtocolFlag_FM",
            "compatible",
            "subghz_scene_protocol_list_validation_text",
        ):
            self.assertIn(required, self.standard_protocol_list)

        self.assertIn(
            "item_context->compatible",
            self.standard_protocol_list,
        )

    def test_standard_and_arf_transmitters_validate_before_radio_initialization(self) -> None:
        for source in (self.standard_txrx, self.arf_txrx):
            validation = source.index("subghz_radio_broker_validate_protocol")
            radio_begin = source.index("subghz_txrx_begin", validation)
            radio_tx = source.index("subghz_txrx_tx", validation)
            self.assertLess(validation, radio_begin)
            self.assertLess(validation, radio_tx)
            self.assertIn("SubGhzTxRxStartTxStateErrorCapability", source)

    def test_reference_gate_protocols_keep_expected_capability_flags(self) -> None:
        expectations = {
            "lib/subghz/protocols/holtek_ht12x.c": (
                "SubGhzProtocolFlag_315",
                "SubGhzProtocolFlag_433",
                "SubGhzProtocolFlag_868",
                "SubGhzProtocolFlag_AM",
                "SubGhzProtocolFlag_FM",
                "SubGhzProtocolFlag_Send",
            ),
            "lib/subghz/protocols/linear.c": (
                "SubGhzProtocolFlag_315",
                "SubGhzProtocolFlag_AM",
                "SubGhzProtocolFlag_Send",
            ),
            "lib/subghz/protocols/chamberlain_code.c": (
                "SubGhzProtocolFlag_315",
                "SubGhzProtocolFlag_AM",
                "SubGhzProtocolFlag_Send",
            ),
        }
        for relative_path, flags in expectations.items():
            source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
            for flag in flags:
                self.assertIn(flag, source, relative_path)

    def test_arf_status_exposes_bounded_runtime_capability_diagnostics(self) -> None:
        for required in (
            '"RF Capabilities"',
            "subghz_protocol_registry_count",
            "subghz_protocol_registry_get_by_index",
            "subghz_radio_broker_protocol_capability",
            '"Holtek_HT12X"',
            '"Linear"',
            '"Cham_Code"',
        ):
            self.assertIn(required, self.arf_status)

        self.assertIn(
            'requires=["gui", "storage", "subghz_radio_broker"]',
            self.arf_status_manifest,
        )
        self.assertIn('fap_version="0.3.0"', self.arf_status_manifest)


if __name__ == "__main__":
    unittest.main()
