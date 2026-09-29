"""Host checks for the bounded RTC crash record and its clear semantics."""

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


RTC_STUB = """
#pragma once
#include <stdint.h>
typedef enum {
    FuriHalRtcRegisterTumoflipCrashMarker = 8,
    FuriHalRtcRegisterTumoflipCrashMeta,
    FuriHalRtcRegisterTumoflipCrashCommit,
    FuriHalRtcRegisterTumoflipCrashApp0,
    FuriHalRtcRegisterTumoflipCrashApp1,
    FuriHalRtcRegisterTumoflipCrashApp2,
    FuriHalRtcRegisterTumoflipCrashChecksum,
    FuriHalRtcRegisterTumoflipActiveApp0,
    FuriHalRtcRegisterTumoflipActiveApp1,
    FuriHalRtcRegisterTumoflipActiveApp2,
    FuriHalRtcRegisterTumoflipActiveCommit,
} FuriHalRtcRegister;
uint32_t furi_hal_rtc_get_register(FuriHalRtcRegister reg);
void furi_hal_rtc_set_register(FuriHalRtcRegister reg, uint32_t value);
"""


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "furi_hal_rtc.h"

static uint32_t registers[20];
static int last_written_register = -1;
uint32_t furi_hal_rtc_get_register(FuriHalRtcRegister reg) { return registers[reg]; }
void furi_hal_rtc_set_register(FuriHalRtcRegister reg, uint32_t value) {
    registers[reg] = value;
    last_written_register = reg;
}

#include "lib/tumoflip_crash_journal/crash_journal.h"

int main(void) {
    TumoflipCrashReport report;
    assert(!tumoflip_crash_journal_read(&report));

    tumoflip_crash_journal_set_active_app("subghz", "abcdef12");
    assert(!tumoflip_crash_journal_read(&report));
    tumoflip_crash_journal_clear_active_app();
    assert(!tumoflip_crash_journal_read(&report));

    tumoflip_crash_journal_set_active_app("garage_door_remote", "abcdef12");
    tumoflip_crash_journal_record(TumoflipCrashKindNullPointer, "abcdef12");
    assert(last_written_register == FuriHalRtcRegisterTumoflipCrashMarker);
    assert(tumoflip_crash_journal_read(&report));
    assert(report.kind == TumoflipCrashKindNullPointer);
    assert(report.sequence == 1);
    assert(report.commit == 0xabcdef12);
    assert(strcmp(report.app_id, "garage_door_") == 0);

    tumoflip_crash_journal_clear_active_app();
    assert(tumoflip_crash_journal_read(&report));
    tumoflip_crash_journal_ack();
    assert(!tumoflip_crash_journal_read(&report));

    tumoflip_crash_journal_set_active_app("nfc", "abcdef12");
    tumoflip_crash_journal_record(TumoflipCrashKindWatchdog, "abcdef12");
    assert(tumoflip_crash_journal_read(&report));
    assert(report.kind == TumoflipCrashKindWatchdog);
    assert(strcmp(report.app_id, "nfc") == 0);
    registers[FuriHalRtcRegisterTumoflipCrashChecksum] ^= 1;
    assert(!tumoflip_crash_journal_read(&report));

    tumoflip_crash_journal_ack();
    tumoflip_crash_journal_set_active_app("private-name", "deadbeef");
    tumoflip_crash_journal_record(TumoflipCrashKindWatchdog, "abcdef12");
    assert(tumoflip_crash_journal_read(&report));
    assert(report.app_id[0] == '\0');

    assert(tumoflip_crash_journal_classify("NULL pointer dereference") ==
           TumoflipCrashKindNullPointer);
    assert(tumoflip_crash_journal_classify("HardFault") == TumoflipCrashKindHardFault);
    assert(tumoflip_crash_journal_classify("private user data") == TumoflipCrashKindOther);
    return 0;
}
"""


class CrashJournalTests(unittest.TestCase):
    def test_rtc_record_survives_popup_and_rejects_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "furi_hal_rtc.h").write_text(RTC_STUB, encoding="utf-8")
            source = root / "main.c"
            source.write_text(HARNESS, encoding="utf-8")
            binary = root / "test_crash_journal"
            command = [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(root), "-I", str(ROOT), str(source), "-o", str(binary),
            ]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
