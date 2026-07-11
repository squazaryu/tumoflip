#!/usr/bin/env python3

import os
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumocard_os"
VM_ROOT = REPO_ROOT / "applications_user/tumovm_poc"


HARNESS = r"""
#include "tumocard_router.h"

#include <assert.h>
#include <string.h>

static TumoVmProgram make_program(uint8_t aid_suffix, const char* name) {
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
    strcpy(program.name, name);
    program.state_size = 8;
    program.aid_size = 7;
    const uint8_t aid[] = {0xF0, 0x54, 0x43, 0x41, 0x52, 0x44, aid_suffix};
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

static size_t select_applet(
    TumoCardRegistry* registry,
    TumoCardSession* session,
    TumoVmTransport transport,
    uint8_t suffix,
    uint8_t response[TUMOVM_RESPONSE_MAX]) {
    const uint8_t select[] = {
        0x00, 0xA4, 0x04, 0x00, 0x07, 0xF0, 0x54, 0x43, 0x41, 0x52, 0x44, suffix,
    };
    return tumocard_process_apdu(
        registry,
        session,
        transport,
        select,
        sizeof(select),
        response,
        TUMOVM_RESPONSE_MAX);
}

int main(void) {
    TumoCardRegistry registry;
    tumocard_registry_init(&registry);
    uint8_t counter_state[8] = {'C', 'O', 'U', 'N', 'T', 0, 0, 0};
    uint8_t notes_state[8] = {'N', 'O', 'T', 'E', 'S', 0, 0, 0};
    TumoVmProgram counter = make_program(0x01, "Counter Token");
    TumoVmProgram notes = make_program(0x02, "Notes Token");

    assert(tumocard_registry_add(&registry, "counter", true, &counter, counter_state) ==
           TumoCardAddResultOk);
    assert(tumocard_registry_add(&registry, "notes", true, &notes, notes_state) ==
           TumoCardAddResultOk);
    TumoVmProgram duplicate_id = make_program(0x03, "Duplicate ID");
    assert(tumocard_registry_add(&registry, "notes", true, &duplicate_id, counter_state) ==
           TumoCardAddResultDuplicateId);
    TumoVmProgram duplicate_aid = make_program(0x01, "Duplicate AID");
    assert(tumocard_registry_add(&registry, "duplicate", true, &duplicate_aid, counter_state) ==
           TumoCardAddResultDuplicateAid);
    assert(tumocard_registry_add(&registry, "Bad/Id", true, &notes, notes_state) ==
           TumoCardAddResultInvalid);

    TumoCardSession usb;
    TumoCardSession nfc;
    tumocard_session_reset(&usb);
    tumocard_session_reset(&nfc);
    uint8_t response[TUMOVM_RESPONSE_MAX];
    const uint8_t read[] = {0x00, 0xB0, 0x00, 0x00, 0x08};
    size_t response_size = tumocard_process_apdu(
        &registry, &usb, TumoVmTransportUsb, read, sizeof(read), response, sizeof(response));
    expect_sw(response, response_size, 0x6985);

    response_size = select_applet(&registry, &usb, TumoVmTransportUsb, 0x7F, response);
    expect_sw(response, response_size, 0x6A82);
    assert(usb.selected_index == -1);

    response_size = select_applet(&registry, &usb, TumoVmTransportUsb, 0x01, response);
    expect_sw(response, response_size, 0x9000);
    assert(usb.selected_index == 0);
    const uint8_t update[] = {0x00, 0xD6, 0x00, 0x05, 0x03, 0x01, 0x02, 0x03};
    response_size = tumocard_process_apdu(
        &registry,
        &usb,
        TumoVmTransportUsb,
        update,
        sizeof(update),
        response,
        sizeof(response));
    expect_sw(response, response_size, 0x9000);

    response_size = select_applet(&registry, &nfc, TumoVmTransportNfc, 0x01, response);
    expect_sw(response, response_size, 0x9000);
    response_size = tumocard_process_apdu(
        &registry, &nfc, TumoVmTransportNfc, read, sizeof(read), response, sizeof(response));
    assert(response_size == 10);
    assert(memcmp(response, "COUNT", 5) == 0);
    assert(response[5] == 0x01 && response[6] == 0x02 && response[7] == 0x03);
    expect_sw(response, response_size, 0x9000);

    response_size = select_applet(&registry, &usb, TumoVmTransportUsb, 0x02, response);
    expect_sw(response, response_size, 0x9000);
    response_size = tumocard_process_apdu(
        &registry, &usb, TumoVmTransportUsb, read, sizeof(read), response, sizeof(response));
    assert(memcmp(response, "NOTES", 5) == 0);

    assert(tumocard_registry_set_enabled(&registry, 1, false));
    response_size = select_applet(&registry, &usb, TumoVmTransportUsb, 0x02, response);
    expect_sw(response, response_size, 0x6A82);
    assert(usb.selected_index == -1);

    const uint8_t malformed[] = {0x00, 0xA4, 0x04};
    response_size = tumocard_process_apdu(
        &registry,
        &usb,
        TumoVmTransportUsb,
        malformed,
        sizeof(malformed),
        response,
        sizeof(response));
    expect_sw(response, response_size, 0x6700);
    return 0;
}
"""


class TumoCardOsTest(unittest.TestCase):
    def test_router_with_native_harness(self) -> None:
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler, "A C compiler is required for the TumoCard host test")
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            harness = temp / "tumocard_harness.c"
            binary = temp / "tumocard_harness"
            harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")
            subprocess.run(
                [
                    str(compiler),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{APP_ROOT}",
                    f"-I{VM_ROOT}",
                    str(harness),
                    str(APP_ROOT / "tumocard_router.c"),
                    str(VM_ROOT / "tumovm_core.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            subprocess.run([str(binary)], check=True)

    def test_package_contract_is_bounded_and_atomic(self) -> None:
        router_header = (APP_ROOT / "tumocard_router.h").read_text(encoding="utf-8")
        router = (APP_ROOT / "tumocard_router.c").read_text(encoding="utf-8")
        package = (APP_ROOT / "tumocard_package.c").read_text(encoding="utf-8")

        self.assertIn("TUMOCARD_APPLET_MAX    4U", router_header)
        self.assertIn("TumoCardAddResultDuplicateAid", router)
        self.assertIn("TUMOCARD_SW_APPLET_NOT_FOUND", router)
        self.assertIn('"manifest.tca"', package)
        self.assertIn('"program.tvm"', package)
        self.assertIn('"state.tmp"', package)
        self.assertIn('"state.bak"', package)
        self.assertIn('"settings.tmp"', package)
        self.assertIn("tumocard_atomic_replace", package)
        self.assertIn("tumocard_mark_invalid", package)
        self.assertIn("tumocard_recover_atomic", package)
        self.assertIn('"Crypto mask"', package)

    def test_reference_applets_and_controls_are_present(self) -> None:
        package = (APP_ROOT / "tumocard_package.c").read_text(encoding="utf-8")
        app = (APP_ROOT / "tumocard_os.c").read_text(encoding="utf-8")
        readme = (APP_ROOT / "README.md").read_text(encoding="utf-8")

        self.assertIn('"Counter Token"', package)
        self.assertIn('"Notes Token"', package)
        self.assertIn("elements_button_left(canvas, \"Prev\")", app)
        self.assertIn("elements_button_center", app)
        self.assertIn("elements_button_right(canvas, \"Next\")", app)
        self.assertIn("tumocard_session_reset(&app->nfc_session)", app)
        self.assertIn("TumoCard OS", readme)

    def test_app_is_packaged_as_api88_source_build(self) -> None:
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        app = (APP_ROOT / "tumocard_os.c").read_text(encoding="utf-8")

        self.assertIn('appid="tumocard_os"', manifest)
        self.assertIn('fap_dist_path="apps/Module One/NFC/tumocard_os.fap"', manifest)
        self.assertIn('../tumovm_poc/tumovm_core.c', manifest)
        self.assertIn('../../applications/debug/ccid_test/ccid_usb.c', manifest)
        self.assertIn('"apps/Module One/NFC/tumocard_os.fap"', validator)
        self.assertNotIn("furi_hal_usb_ccid", app)


if __name__ == "__main__":
    unittest.main()
