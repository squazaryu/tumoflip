import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/nfc_ccid_bridge"
CORE_SOURCE = APP_ROOT / "nfc_ccid_bridge_core.c"


class NfcCcidBridgeTest(unittest.TestCase):
    def test_core_policy_with_host_compiler(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler)
        harness_source = r'''
#include "nfc_ccid_bridge_core.h"
#include <assert.h>

int main(void) {
    const uint8_t select[] = {0x00, 0xA4, 0x04, 0x00, 0x02, 0x12, 0x34};
    const uint8_t read[] = {0x00, 0xB0, 0x00, 0x00, 0x04};
    const uint8_t update[] = {0x00, 0xD6, 0x00, 0x00, 0x01, 0xAA};
    const uint8_t malformed[] = {0x00, 0xD6, 0x00, 0x00, 0x02, 0xAA};
    const uint8_t response[] = {0x10, 0x20, 0x90, 0x00};

    assert(nfc_ccid_bridge_check_apdu(select, sizeof(select), NfcCcidBridgePolicyReadOnly) == NfcCcidBridgeApduAllowed);
    assert(nfc_ccid_bridge_check_apdu(read, sizeof(read), NfcCcidBridgePolicyReadOnly) == NfcCcidBridgeApduAllowed);
    assert(nfc_ccid_bridge_check_apdu(update, sizeof(update), NfcCcidBridgePolicyReadOnly) == NfcCcidBridgeApduBlocked);
    assert(nfc_ccid_bridge_check_apdu(update, sizeof(update), NfcCcidBridgePolicyFull) == NfcCcidBridgeApduAllowed);
    assert(nfc_ccid_bridge_check_apdu(malformed, sizeof(malformed), NfcCcidBridgePolicyFull) == NfcCcidBridgeApduMalformed);
    assert(nfc_ccid_bridge_response_status(response, sizeof(response)) == 0x9000);
    assert(nfc_ccid_bridge_response_status(response, 1) == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            harness = Path(directory) / "bridge_test.c"
            binary = Path(directory) / "bridge_test"
            harness.write_text(harness_source, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{APP_ROOT}",
                    str(harness),
                    str(CORE_SOURCE),
                    "-o",
                    str(binary),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            subprocess.run([str(binary)], check=True)

    def test_nfc_work_stays_in_poller_callback(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        transfer_start = source.index("static void nfc_ccid_transfer(")
        poller_start = source.index("static NfcCommand nfc_ccid_poller_callback(")
        transfer_source = source[transfer_start:poller_start]

        self.assertNotIn("iso14443_4a_poller_send_block", transfer_source)
        self.assertIn("NfcCcidMailboxPending", transfer_source)
        self.assertIn("furi_semaphore_acquire", transfer_source)
        self.assertIn("iso14443_4a_poller_send_block", source[poller_start:])
        self.assertIn("NFC_CCID_REQUEST_TIMEOUT", source)

    def test_full_relay_requires_long_ok_and_read_only_is_immediate(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        self.assertIn("event->type == InputTypeLong", source)
        self.assertIn("app->policy = NfcCcidBridgePolicyFull", source)
        self.assertIn("event->type == InputTypeShort", source)
        self.assertIn("app->policy = NfcCcidBridgePolicyReadOnly", source)

    def test_lifecycle_restores_usb_and_nfc(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        stop_start = source.index("static void nfc_ccid_stop_transports(")
        stop_end = source.index("static NfcCcidBridgeApp*", stop_start)
        stop_source = source[stop_start:stop_end]

        self.assertIn("nfc_ccid_fail_pending", stop_source)
        self.assertLess(stop_source.index("nfc_poller_stop"), stop_source.index("nfc_free"))
        self.assertIn("ccid_usb_set_callbacks(NULL, NULL)", stop_source)
        self.assertIn("furi_hal_usb_set_config(app->previous_usb, NULL)", stop_source)

    def test_release_package_and_privacy_contract(self) -> None:
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")
        readme = (APP_ROOT / "README.md").read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )

        self.assertIn('appid="nfc_ccid_bridge"', manifest)
        self.assertIn('fap_dist_path="apps/Module One/NFC/nfc_ccid_bridge.fap"', manifest)
        self.assertIn("nfc_ccid_usb_backend.c", "\n".join(path.name for path in APP_ROOT.iterdir()))
        self.assertIn('"apps/Module One/NFC/nfc_ccid_bridge.fap"', validator)
        self.assertIn("APDU payloads are never written", readme)
        self.assertNotIn("furi_hal_usb_ccid", (APP_ROOT / "nfc_ccid_bridge.c").read_text())


if __name__ == "__main__":
    unittest.main()
