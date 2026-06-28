#!/usr/bin/env python3

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SERVICE_SOURCE = (
    REPO_ROOT / "targets/f7/ble_glue/services/gatt_service_changed_service.c"
)
SERIAL_PROFILE = REPO_ROOT / "targets/f7/ble_glue/profiles/serial_profile.c"


class BleServiceChangedTest(unittest.TestCase):
    def test_serial_profile_registers_service_changed_helper(self) -> None:
        profile = SERIAL_PROFILE.read_text(encoding="utf-8")

        self.assertIn("gatt_service_changed_service_i.h", profile)
        self.assertIn("ble_svc_gatt_service_changed_start()", profile)
        self.assertIn("ble_svc_gatt_service_changed_mark_dirty", profile)
        self.assertIn("ble_svc_gatt_service_changed_stop", profile)

    def test_helper_uses_stack_builtin_service_changed_handles(self) -> None:
        source = SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("BLE_SVC_GATT_SERVICE_HANDLE", source)
        self.assertIn("0x0001U", source)
        self.assertIn("BLE_SVC_GATT_SERVICE_CHANGED_CHAR_HANDLE", source)
        self.assertIn("0x0002U", source)
        self.assertIn("BLE_SVC_GATT_SERVICE_CHANGED_CCCD_HANDLE", source)
        self.assertIn("0x0004U", source)
        self.assertIn("aci_gatt_update_char_value", source)
        self.assertIn("ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE", source)
        self.assertIn("ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE", source)


if __name__ == "__main__":
    unittest.main()
