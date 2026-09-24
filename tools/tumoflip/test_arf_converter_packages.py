"""Release-scope and lifecycle contracts for the file-only converter."""

from pathlib import Path
import unittest

from tools.tumoflip.validate_release import (
    PACKAGE_ONLY_PACKAGE_FILES,
    PACKAGE_ONLY_PACKAGE_GROUPS,
    package_extapp_exports,
    PROTOCOL_PACKS,
)

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/protopirate_to_subghz"
TARGET = "apps_data/arf_subghz_full/packages/protopirate_to_subghz.fap"


class ArfConverterPackagesTests(unittest.TestCase):
    def test_unrequested_additions_are_not_shipped(self):
        self.assertFalse((ROOT / "applications/system/renault_seed_bf/application.fam").exists())
        for filename in ("renault_v1.c", "renault_v1.h", "renault_v1_plugin.c",
                         "fiat_v1.c", "fiat_v1.h", "hitag2_seed.c", "hitag2_seed.h"):
            self.assertFalse((ROOT / "applications_user/subghz_protocols" / filename).exists())
        self.assertNotIn("protocol_renault_v1.fal", PROTOCOL_PACKS)
        self.assertNotIn("renault_seed_bf.fap", package_extapp_exports())
        for workflow in ("pr-build.yml", "release.yml"):
            source = (ROOT / ".github/workflows" / workflow).read_text()
            self.assertNotIn("fap_renault_seed_bf", source)
            self.assertNotIn("fap_protocol_renault_v1", source)

    def test_arf_renault_v1_modulation_does_not_leak_to_different_local_protocol(self):
        renault_v0 = (ROOT / "lib/subghz/protocols/renault.c").read_text()
        registry = (ROOT / "lib/subghz/protocols/protocol_items.c").read_text()
        self.assertNotIn("SubGhzProtocolFlag_FM", renault_v0)
        self.assertIn("&subghz_protocol_toyota", registry)

    def test_converter_is_package_only_and_reachable_from_hub(self):
        manifest = (APP / "application.fam").read_text()
        self.assertIn("fap_package_only=True", manifest)
        self.assertIn(TARGET, PACKAGE_ONLY_PACKAGE_FILES)
        self.assertEqual(PACKAGE_ONLY_PACKAGE_GROUPS[TARGET], "arf")
        hub = (ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c").read_text()
        self.assertIn('"Capture converter"', hub)
        self.assertIn(TARGET, hub)

    def test_worker_does_not_block_on_dispatcher_event_queue(self):
        source = (APP / "protopirate_to_subghz.c").read_text()
        self.assertNotIn("view_dispatcher_send_custom_event", source)
        self.assertIn("view_dispatcher_set_tick_event_callback", source)
        self.assertIn("FuriThreadStateStopped", source)
        self.assertIn("P2sResultCancelled", source)
        self.assertIn("DirWalkError", source)

    def test_converter_preserves_complete_capture(self):
        source = (APP / "converter.c").read_text()
        self.assertIn("p2s_copy_verified", source)
        self.assertIn("FSAM_READ", source)
        self.assertIn("P2S_FILE_VERSION", source)
        self.assertNotIn("p2s_translate_pp_to_sub", source)
        self.assertNotIn("p2s_copy_extras", source)


if __name__ == "__main__":
    unittest.main()
