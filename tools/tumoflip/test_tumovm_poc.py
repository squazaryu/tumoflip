#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumovm_poc"
CORE_SOURCE = APP_ROOT / "tumovm_core.c"


HARNESS = r"""
#include "tumovm_core.h"

#include <assert.h>
#include <string.h>

static TumoVmProgram make_program(void) {
    static const uint8_t aid[] = {0xF0, 0x54, 0x56, 0x4D, 0x01};
    static const uint8_t code[] = {
        TumoVmOpcodeCheckP1, 0x04,
        TumoVmOpcodeCheckDataAid,
        TumoVmOpcodeSetSelected,
        TumoVmOpcodeReturnStatus, 0x90, 0x00,
        TumoVmOpcodeRequireSelected,
        TumoVmOpcodeReadState,
        TumoVmOpcodeReturnStatus, 0x90, 0x00,
        TumoVmOpcodeRequireSelected,
        TumoVmOpcodeWriteState,
        TumoVmOpcodeReturnStatus, 0x90, 0x00,
    };
    TumoVmProgram program = {0};
    strcpy(program.name, "Shared Object");
    program.state_size = 16;
    program.aid_size = sizeof(aid);
    memcpy(program.aid, aid, sizeof(aid));
    program.route_count = 3;
    program.routes[0] = (TumoVmRoute){0x00, 0xA4, 0};
    program.routes[1] = (TumoVmRoute){0x00, 0xB0, 7};
    program.routes[2] = (TumoVmRoute){0x00, 0xD6, 12};
    program.bytecode_size = sizeof(code);
    memcpy(program.bytecode, code, sizeof(code));
    program.capability_nfc_type4 = true;
    program.capability_usb_ccid = true;
    return program;
}

static void expect_sw(const uint8_t* response, size_t size, uint16_t sw) {
    assert(size >= 2);
    assert(response[size - 2] == (uint8_t)(sw >> 8));
    assert(response[size - 1] == (uint8_t)sw);
}

int main(void) {
    TumoVmProgram program = make_program();
    assert(tumovm_program_validate(&program));
    TumoVmProgram nfc_only = make_program();
    nfc_only.capability_usb_ccid = false;
    assert(tumovm_program_validate(&nfc_only));
    nfc_only.capability_nfc_type4 = false;
    assert(!tumovm_program_validate(&nfc_only));

    uint8_t initial[16];
    for(size_t i = 0; i < sizeof(initial); i++) initial[i] = (uint8_t)i;

    TumoVm vm;
    TumoVmSession usb = {0};
    TumoVmSession nfc = {0};
    uint8_t response[TUMOVM_RESPONSE_MAX];
    tumovm_init(&vm, &program, initial);

    const uint8_t read_before_select[] = {0x00, 0xB0, 0x00, 0x00, 0x04};
    size_t response_size = tumovm_process_apdu(
        &vm, &usb, TumoVmTransportUsb,
        read_before_select, sizeof(read_before_select), response, sizeof(response));
    expect_sw(response, response_size, 0x6985);

    const uint8_t select[] = {
        0x00, 0xA4, 0x04, 0x00, 0x05, 0xF0, 0x54, 0x56, 0x4D, 0x01,
    };
    response_size = tumovm_process_apdu(
        &vm, &usb, TumoVmTransportUsb,
        select, sizeof(select), response, sizeof(response));
    expect_sw(response, response_size, 0x9000);
    assert(usb.selected);
    assert(!nfc.selected);

    const uint8_t update[] = {0x00, 0xD6, 0x00, 0x02, 0x02, 0xAA, 0xBB};
    response_size = tumovm_process_apdu(
        &vm, &usb, TumoVmTransportUsb,
        update, sizeof(update), response, sizeof(response));
    expect_sw(response, response_size, 0x9000);
    assert(vm.dirty);
    assert(vm.write_count == 1);

    response_size = tumovm_process_apdu(
        &vm, &nfc, TumoVmTransportNfc,
        select, sizeof(select), response, sizeof(response));
    expect_sw(response, response_size, 0x9000);
    response_size = tumovm_process_apdu(
        &vm, &nfc, TumoVmTransportNfc,
        read_before_select, sizeof(read_before_select), response, sizeof(response));
    assert(response_size == 6);
    assert(response[0] == 0x00 && response[1] == 0x01);
    assert(response[2] == 0xAA && response[3] == 0xBB);
    expect_sw(response, response_size, 0x9000);

    const uint8_t malformed[] = {0x00, 0xA4, 0x04};
    response_size = tumovm_process_apdu(
        &vm, &usb, TumoVmTransportUsb,
        malformed, sizeof(malformed), response, sizeof(response));
    expect_sw(response, response_size, 0x6700);

    TumoVmProgram invalid = make_program();
    invalid.bytecode[0] = 0xFF;
    assert(!tumovm_program_validate(&invalid));
    invalid = make_program();
    invalid.routes[2] = invalid.routes[1];
    assert(!tumovm_program_validate(&invalid));
    return 0;
}
"""


class TumoVmPocTest(unittest.TestCase):
    def test_core_interpreter_with_native_harness(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "A C compiler is required for the TumoVM host test")
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            harness = temp / "tumovm_harness.c"
            binary = temp / "tumovm_harness"
            harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")
            subprocess.run(
                [
                    str(compiler),
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

    def test_app_is_packaged_as_api88_source_build(self) -> None:
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")
        self.assertIn('appid="tumovm_poc"', manifest)
        self.assertIn('fap_dist_path="apps/Module One/Labs/tumovm_poc.fap"', manifest)
        self.assertIn('../../applications/debug/ccid_test/ccid_usb.c', manifest)
        self.assertNotIn("furi_hal_usb_ccid", (APP_ROOT / "tumovm_poc.c").read_text())

        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text()
        self.assertIn('"apps/Module One/Labs/tumovm_poc.fap"', validator)

    def test_package_is_bounded_and_fails_closed(self) -> None:
        core = (APP_ROOT / "tumovm_core.c").read_text(encoding="utf-8")
        package = (APP_ROOT / "tumovm_package.c").read_text(encoding="utf-8")
        header = (APP_ROOT / "tumovm_core.h").read_text(encoding="utf-8")

        self.assertIn("TUMOVM_INSTRUCTION_MAX", header)
        self.assertIn("TUMOVM_BYTECODE_MAX", header)
        self.assertIn("tumovm_program_validate", core)
        self.assertIn("TumoVmPackageResultInvalidProgram", package)
        self.assertIn("TumoVmPackageResultInvalidState", package)
        self.assertIn("TUMOVM_STATE_BAK_PATH", package)

    def test_nfc_listener_uses_the_correct_parent_protocol(self) -> None:
        source = (
            REPO_ROOT / "lib/nfc/protocols/type_4_tag/type_4_tag_listener.c"
        ).read_text(encoding="utf-8")
        self.assertIn("event.protocol == NfcProtocolIso14443_4a", source)
        self.assertNotIn("event.protocol == NfcProtocolIso15693_3", source)

    def test_app_local_ccid_worker_has_shutdown_and_bounds_guards(self) -> None:
        source = (
            REPO_ROOT / "applications/debug/ccid_test/ccid_usb.c"
        ).read_text(encoding="utf-8")
        self.assertIn("memset(ccid_usb, 0, sizeof(CcidUsb));", source)
        self.assertIn("if(flags & WorkerEvtStop) break;", source)
        self.assertIn("message->dwLength > CCID_DATABLOCK_SIZE", source)
        self.assertIn("sizeof(ccid_usb->receive_buffer) -", source)
        self.assertIn("requestSetParametersT0->bProtocolNum != 0x00", source)
        self.assertIn("free(ccid_usb_interface.str_manuf_descr);", source)
        self.assertIn("furi_semaphore_free(ccid_usb->ccid_semaphore);", source)
        self.assertIn("furi_ms_to_ticks(1000)", source)

        app_source = (APP_ROOT / "tumovm_poc.c").read_text(encoding="utf-8")
        stop_index = app_source.index("tumovm_stop_transports(app);", app_source.index("tumovm_app_free"))
        save_index = app_source.index("tumovm_refresh_model(app);", app_source.index("tumovm_app_free"))
        self.assertLess(stop_index, save_index)

    def test_macos_ccid_identity_and_smoke_contract(self) -> None:
        app_source = (APP_ROOT / "tumovm_poc.c").read_text(encoding="utf-8")
        readme = (APP_ROOT / "README.md").read_text(encoding="utf-8")
        smoke = (REPO_ROOT / "tools/tumoflip/tumovm_ccid_smoke.py").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define TUMOVM_CCID_VID 0x076BU", app_source)
        self.assertIn("#define TUMOVM_CCID_PID 0x3A21U", app_source)
        self.assertIn("prototype compatibility measure", readme)
        self.assertIn('bytes.fromhex("00 A4 04 00 05 F0 54 56 4D 01")', smoke)
        self.assertIn('bytes.fromhex("00 B0 00 00 04")', smoke)
        self.assertIn("RESTORE original", smoke)


if __name__ == "__main__":
    unittest.main()
