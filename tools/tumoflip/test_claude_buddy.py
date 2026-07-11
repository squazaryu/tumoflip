#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ClaudeBuddyIntegrationTest(unittest.TestCase):
    def test_tumoflip_variant_is_built_and_packaged(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/claude_buddy/application.fam"
        ).read_text(encoding="utf-8")
        source = (
            REPO_ROOT / "applications_user/claude_buddy/claude_buddy.c"
        ).read_text(encoding="utf-8")
        settings = (
            REPO_ROOT / "applications_user/claude_buddy/app_settings.c"
        ).read_text(encoding="utf-8")
        fbt_options = (REPO_ROOT / "fbt_options.py").read_text(encoding="utf-8")
        release = (
            REPO_ROOT / "tools/tumoflip/validate_release.py"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="claude_buddy"', manifest)
        self.assertIn('fap_version="0.6.1"', manifest)
        self.assertIn("Merged build: always the standard-serial Bridge profile", source)
        self.assertIn("cb_transcript_log", source)
        self.assertIn("return BleModeBridge", settings)
        self.assertNotIn('    "claude_buddy",', fbt_options)
        self.assertIn('resources / "apps/Bluetooth/claude_buddy.fap"', release)


if __name__ == "__main__":
    unittest.main()
