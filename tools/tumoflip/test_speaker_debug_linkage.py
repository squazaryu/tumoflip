"""Ensure Speaker Debug links its private note-frequency helper instead of widening API."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class SpeakerDebugLinkageTests(unittest.TestCase):
    def test_fap_compiles_private_note_frequency_helper(self):
        manifest = (ROOT / "applications/debug/speaker_debug/application.fam").read_text(
            encoding="utf-8"
        )
        api = (ROOT / "targets/f7/api_symbols.csv").read_text(encoding="utf-8")

        self.assertRegex(
            manifest,
            r"sources\s*=\s*\[[^\]]*\.\./\.\./lib/toolbox/note_frequency\.c",
        )
        self.assertNotIn("Function,+,note_frequency_from_semitone,", api)


if __name__ == "__main__":
    unittest.main()
