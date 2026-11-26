"""Core activity/recovery policy on synthetic counters, time and RTC only."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "furi/core"

MODEL_TEST = r"""
#include <assert.h>
#include <limits.h>
#include <string.h>
#include "system_policy_i.h"

int main(void) {
    TumoSystemState s;
    tumo_system_model_init(&s);
    assert(s.display_on && s.phone_allowed && !tumo_system_model_quiet(&s));
    s.display_on=false; assert(tumo_system_model_quiet(&s));
    assert(tumo_system_model_acquire(&s,TumoSystemPhonePreview));
    assert(!tumo_system_model_quiet(&s) && s.phone_previews==1);
    assert(tumo_system_model_acquire(&s,TumoSystemPreview));
    tumo_system_model_release(&s,TumoSystemPhonePreview);
    assert(!tumo_system_model_quiet(&s));
    tumo_system_model_release(&s,TumoSystemPreview);
    tumo_system_model_release(&s,TumoSystemPreview);
    assert(s.previews==0 && tumo_system_model_quiet(&s));
    s.recovery=true; s.display_on=true;
    assert(tumo_system_model_quiet(&s)); // Safe start stays static even with a preview.

    tumo_system_model_init(&s);
    for(unsigned i=0;i<255;i++) assert(tumo_system_model_acquire(&s,TumoSystemWrite));
    assert(!tumo_system_model_acquire(&s,TumoSystemWrite));
    assert(s.writes==255 && tumo_system_model_busy(&s,0));
    for(unsigned i=0;i<256;i++) tumo_system_model_release(&s,TumoSystemWrite);
    assert(s.writes==0);
    assert(!tumo_system_model_acquire(&s,99));
    tumo_system_model_release(&s,99);
    assert(tumo_system_model_transfer(&s,true,UINT32_MAX-500,1000));
    assert(tumo_system_model_busy(&s,200));
    assert(!tumo_system_model_busy(&s,600)); // Wraparound and expiry.
    assert(!tumo_system_model_quiesce(&s,TumoSystemRestart,600,5000)); // Not safe mode.
    s.recovery=true; s.foreground=true;
    assert(!tumo_system_model_quiesce(&s,TumoSystemRestart,600,5000));
    s.foreground=false;
    assert(tumo_system_model_quiesce(&s,TumoSystemRestart,600,5000));
    assert(!tumo_system_model_acquire(&s,TumoSystemWrite));
    assert(!tumo_system_model_acquire(&s,TumoSystemPreview));
    assert(!tumo_system_model_transfer(&s,true,700,1000));
    assert(!tumo_system_model_quiesce(&s,TumoSystemRestart,700,5000));
    tumo_system_model_expire(&s,100000);
    assert(s.closing==TumoSystemRestart); // A restart barrier cannot lapse into writes.

    tumo_system_model_init(&s);
    assert(!tumo_system_model_quiesce(&s,TumoSystemPhoneDisconnect,0,5000));
    assert(tumo_system_model_acquire(&s,TumoSystemPhonePreview));
    assert(tumo_system_model_acquire(&s,TumoSystemWrite));
    assert(!tumo_system_model_quiesce(&s,TumoSystemPhoneDisconnect,0,5000));
    assert(s.phone_allowed);
    tumo_system_model_release(&s,TumoSystemWrite);
    assert(tumo_system_model_transfer(&s,true,0,1000));
    assert(!tumo_system_model_quiesce(&s,TumoSystemPhoneDisconnect,0,5000));
    assert(tumo_system_model_transfer(&s,false,0,1000));
    assert(tumo_system_model_quiesce(&s,TumoSystemPhoneDisconnect,0,5000));
    assert(!s.phone_allowed && s.closing==TumoSystemPhoneDisconnect);
    tumo_system_model_expire(&s,4999); assert(s.closing==TumoSystemPhoneDisconnect);
    tumo_system_model_release(&s,TumoSystemPhonePreview);
    tumo_system_model_expire(&s,4999); assert(s.closing==TumoSystemNotClosing);
    assert(tumo_system_model_acquire(&s,TumoSystemWrite));
    assert(!tumo_system_model_acquire(&s,TumoSystemPhonePreview));
    assert(tumo_system_model_acquire(&s,TumoSystemPreview)); // Other hosts unaffected.
    tumo_system_model_release(&s,TumoSystemWrite);
    s.phone_allowed=true;
    assert(tumo_system_model_acquire(&s,TumoSystemPhonePreview));
    assert(tumo_system_model_quiesce(&s,TumoSystemPhoneDisconnect,5000,5000));
    tumo_system_model_expire(&s,10000);
    assert(s.closing==TumoSystemNotClosing && !s.phone_allowed); // Failed stop releases writes.

    return 0;
}
"""

FURI_STUB = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
extern unsigned critical_depth;
#define FURI_CRITICAL_ENTER() do { critical_depth++; } while(0)
#define FURI_CRITICAL_EXIT() do { assert(critical_depth); critical_depth--; } while(0)
uint32_t furi_get_tick(void);
uint32_t furi_ms_to_ticks(uint32_t milliseconds);
"""

RTC_STUB = r"""
#pragma once
#include <stdint.h>
typedef enum {
    FuriHalRtcRegisterTumoflipCrashMarker=8,
    FuriHalRtcRegisterTumoflipCrashMeta=9,
    FuriHalRtcRegisterTumoflipCrashChecksum=14,
} FuriHalRtcRegister;
uint32_t furi_hal_rtc_get_register(FuriHalRtcRegister reg);
void furi_hal_rtc_set_register(FuriHalRtcRegister reg,uint32_t word);
"""

WRAPPER_TEST = r"""
#include <assert.h>
#include <string.h>
#include "furi.h"
#include "furi_hal_rtc.h"
#include "system_policy_i.h"
#include "lib/tumoflip_crash_journal/crash_journal.h"

unsigned critical_depth;
static uint32_t registers[20], tick, callbacks;
uint32_t furi_get_tick(void) { return tick; }
uint32_t furi_ms_to_ticks(uint32_t ms) { return ms; }
uint32_t furi_hal_rtc_get_register(FuriHalRtcRegister reg) { assert(reg<20); return registers[reg]; }
void furi_hal_rtc_set_register(FuriHalRtcRegister reg,uint32_t word) {
    assert(reg<20); registers[reg]=word;
}
static void changed(void* context) { assert(!critical_depth && context==&callbacks); callbacks++; }
int main(int argc,char**argv) {
    assert(argc==2);
    for(unsigned i=0;i<8;i++) registers[i]=0xabc000+i;
    if(!strcmp(argv[1],"recovery")) {
        tumoflip_crash_journal_record(TumoflipCrashKindOther,"12345678");
        tumoflip_crash_journal_record(TumoflipCrashKindOther,"12345678");
        tumoflip_crash_journal_record(TumoflipCrashKindOutOfMemory,"12345678");
        TumoSystemState s=furi_system_policy_snapshot();
        assert(s.recovery && s.boot_crashes==3);
        assert(furi_system_policy_is_recovery());
        TumoflipCrashReport report;
        assert(tumoflip_crash_journal_read(&report) && report.sequence==3);
        uint32_t meta=registers[9];
        for(unsigned bit=16;bit<24;bit++) {
            registers[9]=meta^(1UL<<bit);
            assert(tumoflip_crash_journal_boot_streak()==0);
            assert(tumoflip_crash_journal_read(&report) && report.sequence==3);
        }
        registers[9]=meta;
        tumoflip_crash_journal_ack();
        assert(tumoflip_crash_journal_boot_streak()==3); // Acknowledging the popup cannot reset the guard.
        tumoflip_crash_journal_record(TumoflipCrashKindOther,"12345678");
        assert(tumoflip_crash_journal_boot_streak()==3);
        assert(tumoflip_crash_journal_read(&report) && report.sequence==1);
        assert(furi_system_policy_acquire(TumoSystemWrite));
        assert(!furi_system_policy_prepare_normal_restart());
        furi_system_policy_release(TumoSystemWrite);
        assert(furi_system_policy_prepare_normal_restart());
        assert(!furi_system_policy_acquire(TumoSystemWrite));
        assert(tumoflip_crash_journal_read(&report) && report.sequence==1);
    } else if(!strcmp(argv[1],"healthy")) {
        tumoflip_crash_journal_record(TumoflipCrashKindOther,"12345678");
        tumoflip_crash_journal_record(TumoflipCrashKindOther,"12345678");
        TumoSystemState s=furi_system_policy_snapshot();
        assert(!s.recovery && s.boot_crashes==2);
        tick=59999; furi_system_policy_snapshot();
        assert(tumoflip_crash_journal_boot_streak()==2);
        tick=60000; furi_system_policy_snapshot();
        assert(tumoflip_crash_journal_boot_streak()==0);
        TumoflipCrashReport report;
        assert(tumoflip_crash_journal_read(&report) && report.sequence==2);
    } else {
        furi_system_policy_listen(changed,&callbacks);
        furi_system_policy_application(true);
        assert(furi_system_policy_snapshot().foreground);
        furi_system_policy_application(false);
        assert(!furi_system_policy_snapshot().foreground);
        assert(furi_system_policy_transfer(true) && furi_system_policy_snapshot().transfer);
        assert(furi_system_policy_transfer(false) && !furi_system_policy_snapshot().transfer);
        assert(furi_system_policy_acquire(TumoSystemPhonePreview));
        furi_system_policy_display(false);
        assert(!tumo_system_model_quiet(&(TumoSystemState){.phone_previews=1}));
        assert(furi_system_policy_acquire(TumoSystemWrite));
        assert(!furi_system_policy_pause_phone_preview());
        furi_system_policy_release(TumoSystemWrite);
        assert(furi_system_policy_pause_phone_preview());
        assert(!furi_system_policy_acquire(TumoSystemPhonePreview));
        furi_system_policy_release(TumoSystemPhonePreview);
        TumoSystemState s=furi_system_policy_snapshot();
        assert(s.closing==TumoSystemNotClosing && !s.phone_allowed);
        assert(furi_system_policy_allow_phone_preview());
        assert(callbacks>=5 && !critical_depth);
    }
    for(unsigned i=0;i<8;i++) assert(registers[i]==0xabc000+i);
    assert(!critical_depth);
    return 0;
}
"""


class SystemPolicyTests(unittest.TestCase):
    def compile_case(self, source, wrapper=False):
        temp = tempfile.TemporaryDirectory(prefix="system-policy-host-")
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        (root / "case.c").write_text(source)
        sources = [CORE / "system_policy_model.c"]
        if wrapper:
            (root / "furi.h").write_text(FURI_STUB)
            (root / "furi_hal_rtc.h").write_text(RTC_STUB)
            sources += [CORE / "system_policy.c", CORE / "crash_journal.c"]
        binary = root / "policy-test"
        result = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(root), "-iquote", str(CORE), "-I", str(ROOT), "-I", str(ROOT / "lib"),
            str(root / "case.c"), *map(str, sources), "-o", str(binary),
        ], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return binary

    def test_counters_quiet_policy_and_restart_barriers(self):
        subprocess.run([str(self.compile_case(MODEL_TEST))], check=True, timeout=10)

    def test_real_crash_record_requests_recovery_without_erasing_the_report(self):
        subprocess.run([str(self.compile_case(WRAPPER_TEST, True)), "recovery"], check=True, timeout=10)

    def test_stable_uptime_clears_only_the_recovery_streak(self):
        subprocess.run([str(self.compile_case(WRAPPER_TEST, True)), "healthy"], check=True, timeout=10)

    def test_core_events_notify_outside_critical_sections(self):
        subprocess.run([str(self.compile_case(WRAPPER_TEST, True)), "activity"], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
