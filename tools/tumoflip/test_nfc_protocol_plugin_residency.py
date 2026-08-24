"""Regression checks for the NFC protocol-scene residency reduction (#346).

Tumoflip carries the upstream #1073 work through the reviewed plugin-safety
series (#388), with local API-v3 and location-sidecar extensions.  These checks
prevent a future manifest or scene edit from silently pinning protocol code in
the resident NFC FAP again.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "applications/main/nfc/application.fam"
BASE = ROOT / "applications/main/nfc/helpers/protocol_support/nfc_protocol_support_base.h"
SUPPORT = ROOT / "applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c"
API = ROOT / "applications/main/nfc/api/nfc_app_api_table_i.h"
CLASSIC_THUNK = ROOT / "applications/main/nfc/scenes/nfc_scene_mf_classic_dict_attack.c"
UL_AES_THUNK = ROOT / "applications/main/nfc/scenes/nfc_scene_mf_ultralight_aes_dict_attack.c"


class NfcProtocolPluginResidencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = MANIFEST.read_text()
        cls.base = BASE.read_text()
        cls.support = SUPPORT.read_text()
        cls.api = API.read_text()

    def test_protocol_scene_sources_are_excluded_from_resident_app(self):
        self.assertIn('"!*_extra_scenes.c"', self.manifest)
        self.assertIn('"!mosgortrans"', self.manifest)
        self.assertIn('"!gallagher"', self.manifest)
        self.assertIn('"api/mosgortrans/mosgortrans_util.c"', self.manifest)
        self.assertIn('"api/gallagher/gallagher_util.c"', self.manifest)

    def test_plugin_scene_dispatch_is_abi_guarded(self):
        self.assertIn("extra_scenes", self.base)
        self.assertIn("#define NFC_PROTOCOL_SUPPORT_PLUGIN_API_VERSION 3", self.base)
        self.assertIn("nfc_protocol_support_extra_on_enter", self.support)
        self.assertIn("nfc_protocol_support_extra_on_event", self.support)
        self.assertIn("nfc_protocol_support_extra_on_exit", self.support)

    def test_moved_scene_files_are_thunks(self):
        for source in (CLASSIC_THUNK, UL_AES_THUNK):
            text = source.read_text()
            self.assertIn("nfc_protocol_support_extra_on_enter", text)
            self.assertIn("nfc_protocol_support_extra_on_event", text)
            self.assertIn("nfc_protocol_support_extra_on_exit", text)
            self.assertLess(len(text), 1200)

    def test_local_extensions_remain_exported(self):
        self.assertIn("nfc_location_sidecar", self.manifest)
        self.assertIn("nfc_show_loading_label_popup", self.api)
        self.assertIn("nfc_save_shadow_file", self.api)


if __name__ == "__main__":
    unittest.main()
