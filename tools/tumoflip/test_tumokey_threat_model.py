#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
THREAT_MODEL = REPO_ROOT / "docs/security/tumokey-threat-model.md"


class TumoKeyThreatModelTest(unittest.TestCase):
    def test_security_gate_is_explicit_and_complete(self) -> None:
        model = THREAT_MODEL.read_text(encoding="utf-8")

        for required in (
            "Conditional GO for dev-only Phase B-D hardware testing",
            "USB HID and NFC ISO-DEP are enabled for dev testing",
            "separate dev-only FAP",
            "AES-GCM",
            "mbedtls_platform_zeroize",
            "bounded checked RNG API",
            "sealed integrity structure",
            "Never use `furi_check`",
            "`furi_crash` on host-controlled values",
            "device-local packed certificate",
            "must not be primary or sole account credentials",
            "Stable-channel inclusion is explicitly denied",
            "Repository: squazaryu/tumoflip",
            "MinorGlitch/zerofido@56b433f9d1390ad224246c85a06083b40de4903e",
        ):
            self.assertIn(required, model)

    def test_required_threat_model_sections_exist(self) -> None:
        model = THREAT_MODEL.read_text(encoding="utf-8")

        for heading in (
            "## Overview",
            "## Threat Model, Trust Boundaries, and Assumptions",
            "## Attack Surface, Mitigations, and Attacker Stories",
            "## Severity Calibration (Critical, High, Medium, Low)",
            "## Decision",
        ):
            self.assertIn(heading, model)


if __name__ == "__main__":
    unittest.main()
