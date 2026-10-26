"""Passive sniffer is an explicit managed FAP, not a resident/background service."""
from pathlib import Path
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/nfc_field_sniffer"


class NfcFieldDeliveryTests(unittest.TestCase):
    def test_sniffer_manifest_is_buildable_and_package_only(self):
        apps = []
        if (APP / "application.fam").exists():
            exec(compile((APP / "application.fam").read_text(), "application.fam", "exec"), {
                "App": lambda **values: apps.append(values),
                "FlipperAppType": SimpleNamespace(EXTERNAL=1),
            })
        self.assertEqual(len(apps), 1, "Passive sniffer FAP target is absent")
        app = apps[0]
        self.assertEqual(app["appid"], "tumo_nfc_sniffer")
        self.assertTrue(app["fap_package_only"])
        for name in app["sources"]:
            self.assertTrue((APP / name).is_file(), name)


if __name__ == "__main__":
    unittest.main()
