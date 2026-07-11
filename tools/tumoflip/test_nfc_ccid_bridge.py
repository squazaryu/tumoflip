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
    assert(nfc_ccid_bridge_amplitude_indicates_removal(150, 132, 149, true));
    assert(!nfc_ccid_bridge_amplitude_indicates_removal(150, 132, 134, true));
    assert(nfc_ccid_bridge_amplitude_indicates_removal(0, 132, 145, false));
    assert(!nfc_ccid_bridge_amplitude_indicates_removal(0, 132, 136, false));
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

    def test_back_uses_two_phase_poller_shutdown(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        input_start = source.index("static bool nfc_ccid_input_callback(")
        input_end = source.index("static void nfc_ccid_write_status", input_start)
        input_source = source[input_start:input_end]
        back_source = input_source[: input_source.index("if(event->key != InputKeyOk)")]
        poller_start = source.index("static NfcCommand nfc_ccid_poller_callback(")
        poller_end = source.index("static bool nfc_ccid_start_usb", poller_start)
        poller_source = source[poller_start:poller_end]

        self.assertIn("InputKeyBack", input_source)
        self.assertIn("app->stopping = true", back_source)
        self.assertIn("furi_semaphore_release(app->request_done)", back_source)
        self.assertNotIn("FuriWaitForever", back_source)
        self.assertNotIn("nfc_poller_stop", back_source)
        self.assertIn("return NfcCommandStop", poller_source)
        self.assertIn("NFC_CCID_EVENT_CLOSE", poller_source)
        self.assertIn("view_dispatcher_stop", source)

    def test_slot_change_notification_is_filled_after_state_change(self) -> None:
        source = (REPO_ROOT / "applications/debug/ccid_test/ccid_usb.c").read_text(
            encoding="utf-8"
        )
        notify_start = source.index("void CCID_NotifySlotChange(")
        notify_end = source.index("void ccid_usb_insert_smartcard", notify_start)
        notify_source = source[notify_start:notify_end]

        self.assertIn("message->bMessageType = RDR_TO_PC_NOTIFYSLOTCHANGE", notify_source)
        self.assertIn("inserted ? 0x03 : 0x02", notify_source)
        self.assertNotIn("inserted != ccid_usb->smartcard_inserted", notify_source)
        self.assertIn("smartcard_target_inserted", source)
        self.assertIn(
            "flags & (WorkerEvtInsertSmartcard | WorkerEvtRemoveSmartcard)", source
        )

    def test_iso14443_4a_reset_restarts_protocol_session(self) -> None:
        source = (
            REPO_ROOT / "lib/nfc/protocols/iso14443_4a/iso14443_4a_poller.c"
        ).read_text(encoding="utf-8")
        run_start = source.index("static NfcCommand iso14443_4a_poller_run(")
        run_end = source.index("static bool iso14443_4a_poller_detect(", run_start)
        run_source = source[run_start:run_end]

        self.assertIn("command == NfcCommandReset", run_source)
        self.assertIn("Iso14443_4aPollerStateIdle", run_source)

    def test_ui_exposes_card_and_relay_progress(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        self.assertIn('return "Scanning for card"', source)
        self.assertIn('return "Card ready"', source)
        self.assertIn('return "Relaying APDU"', source)
        self.assertIn('return "Exiting"', source)

    def test_active_card_idle_path_yields_to_gui(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        poller_start = source.index("static NfcCommand nfc_ccid_poller_callback(")
        poller_end = source.index("static bool nfc_ccid_start_usb", poller_start)
        poller_source = source[poller_start:poller_end]
        idle_start = poller_source.index("if(request_size == 0U)")
        idle_source = poller_source[idle_start : poller_source.index("bit_buffer_copy_bytes")]

        self.assertIn("furi_delay_ms(NFC_CCID_ACTIVE_IDLE_MS)", idle_source)
        self.assertIn("return NfcCommandContinue", idle_source)
        self.assertRegex(source, r"#define\s+NFC_CCID_ACTIVE_IDLE_MS\s+5U")

    def test_relay_uses_emv_compatible_transport_and_reports_errors(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        poller_start = source.index("static NfcCommand nfc_ccid_poller_callback(")
        poller_end = source.index("static bool nfc_ccid_start_usb", poller_start)
        poller_source = source[poller_start:poller_end]

        self.assertIn("iso14443_4a_poller_send_block_pwt_ext", poller_source)
        self.assertNotIn("iso14443_4a_poller_send_block(", poller_source)
        self.assertIn('return "PROTO"', source)
        self.assertIn('return "TIMEOUT"', source)
        self.assertIn("nfc_ccid_result_from_error(error)", poller_source)

    def test_card_removal_uses_debounced_amplitude_sensing(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        self.assertIn("ST25R3916_CMD_MEASURE_AMPLITUDE", source)
        self.assertIn("ST25R3916_REG_AD_RESULT", source)
        self.assertIn("ST25R3916_IRQ_MASK_DCT", source)
        self.assertIn("NFC_CCID_PRESENCE_GRACE_MS", source)
        self.assertIn("last_activity_tick", source)
        self.assertIn("presence_monitor_armed", source)
        self.assertIn("if(result == NfcCcidResultOk)", source)
        self.assertIn("nfc_ccid_bridge_amplitude_indicates_removal", source)
        self.assertIn("NFC_CCID_REMOVAL_SAMPLES", source)
        self.assertIn("ccid_usb_remove_smartcard", source)
        self.assertIn("app->session = NfcCcidSessionScanning", source)
        self.assertIn("app->presence_monitor_armed = false", source)

    def test_about_and_button_hints_are_real_screens(self) -> None:
        source = (APP_ROOT / "nfc_ccid_bridge.c").read_text(encoding="utf-8")
        self.assertIn("NfcCcidScreenAbout", source)
        self.assertIn("nfc_ccid_draw_about", source)
        self.assertIn("I_Pin_back_arrow_10x8", source)
        self.assertIn("I_ButtonRight_4x7", source)
        self.assertIn('"EMV: Visa / MC / Mir"', source)
        self.assertIn('"No Classic / Ultralight"', source)
        self.assertIn('"Version 0.1.6"', source)
        self.assertIn('"github.com/squazaryu"', source)
        self.assertIn('"/tumoflip"', source)

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
