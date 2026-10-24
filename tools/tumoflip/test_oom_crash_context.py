"""Native RTC OOM records, legacy-wire compatibility and crash-path ownership."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_crash_journal import RTC_STUB, HARNESS
from tools.tumoflip.test_subremote_lifecycle import function

ROOT = Path(__file__).resolve().parents[2]
EXTRA_REGISTERS = """
    FuriHalRtcRegisterTumoflipOOMMarker,
    FuriHalRtcRegisterTumoflipOOMRequested,
    FuriHalRtcRegisterTumoflipOOMFree,
    FuriHalRtcRegisterTumoflipOOMLargest,
    FuriHalRtcRegisterTumoflipOOMTicks,
    FuriHalRtcRegisterTumoflipOOMThread0,
    FuriHalRtcRegisterTumoflipOOMThread1,
    FuriHalRtcRegisterTumoflipOOMThread2,
    FuriHalRtcRegisterTumoflipOOMChecksum,
"""
STUB = RTC_STUB if "FuriHalRtcRegisterTumoflipOOMMarker" in RTC_STUB else RTC_STUB.replace("    FuriHalRtcRegisterTumoflipActiveCommit,", "    FuriHalRtcRegisterTumoflipActiveCommit," + EXTRA_REGISTERS)
PREAMBLE = HARNESS[:HARNESS.index("int main(void)")].replace("registers[20]", "registers[32]")


def native(code):
    with tempfile.TemporaryDirectory(prefix="oom-crash-test-") as directory:
        work = Path(directory)
        (work / "furi_hal_rtc.h").write_text(STUB)
        (work / "test.c").write_text(code)
        result = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(work), "-I", str(ROOT), str(work / "test.c"), "-o", str(work / "test"),
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(work / "test")], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)


class OomCrashContextTests(unittest.TestCase):
    def test_out_of_memory_is_not_generic_other(self):
        native(PREAMBLE + r'''
int main(void){
 assert((unsigned)tumoflip_crash_journal_classify("out of memory")==8);
 assert(tumoflip_crash_journal_classify("malloc(0)")==TumoflipCrashKindOther);
 assert(tumoflip_crash_journal_classify(NULL)==TumoflipCrashKindOther);
 return 0;
}
''')

    def test_oom_context_roundtrip_corruption_legacy_and_ack(self):
        native(PREAMBLE + r'''
int main(void){
 TumoflipCrashReport report;
 TumoflipOOMContext context={.requested=UINT32_MAX,.free_heap=114672,.largest_block=113448,
                            .tick_ms=UINT32_MAX,.thread="BtSrv;payloa"};
 // A legacy record remains readable with no invented allocation context.
 tumoflip_crash_journal_record(TumoflipCrashKindOther,"8aaa6856");
 assert(tumoflip_crash_journal_read(&report)&&!report.has_oom_context);
 tumoflip_crash_journal_set_active_app("owned-app","ddfbeb36");
 tumoflip_crash_journal_record(TumoflipCrashKindOutOfMemory,"ddfbeb36");
 tumoflip_crash_journal_attach_oom(&context);
 assert(tumoflip_crash_journal_read(&report));
 assert(report.kind==TumoflipCrashKindOutOfMemory&&report.has_oom_context);
 assert(report.sequence==2&&!strcmp(report.app_id,"owned-app"));
 assert(report.oom.requested==UINT32_MAX&&report.oom.free_heap==114672);
 assert(report.oom.largest_block==113448&&report.oom.tick_ms==UINT32_MAX);
 assert(!strcmp(report.oom.thread,"BtSrv_payloa"));
 // Closing the popup/app must not erase the previous failure snapshot.
 tumoflip_crash_journal_clear_active_app();
 assert(tumoflip_crash_journal_read(&report)&&report.has_oom_context);
 for(unsigned reg=FuriHalRtcRegisterTumoflipOOMRequested;
     reg<=FuriHalRtcRegisterTumoflipOOMChecksum;reg++){
  registers[reg]^=1;
  assert(tumoflip_crash_journal_read(&report)&&!report.has_oom_context);
  registers[reg]^=1;
 }
 // A later legacy/non-OOM crash cannot inherit stale OOM details.
 tumoflip_crash_journal_record(TumoflipCrashKindWatchdog,"ddfbeb36");
 assert(tumoflip_crash_journal_read(&report)&&!report.has_oom_context);
 tumoflip_crash_journal_attach_oom(NULL);
 assert(tumoflip_crash_journal_read(&report)&&!report.has_oom_context);
 tumoflip_crash_journal_ack();assert(!tumoflip_crash_journal_read(&report));
 return 0;
}
''')

    def test_capture_before_logging_preserves_actual_requested_size(self):
        allocator = (ROOT / "furi/core/memmgr_heap.c").read_text()
        self.assertIn("__furi_crash_oom(xToWipe)", allocator)
        check = (ROOT / "furi/core/check.c").read_text().replace("FURI_NORETURN ", "")
        crash = function(check, "__furi_crash_implementation")
        self.assertLess(crash.index("tumoflip_crash_journal_record("), crash.index("furi_log_puts("))
        capture = function(check, "__furi_crash_oom")
        for forbidden in ("malloc(", "calloc(", "storage_", "furi_string_"):
            self.assertNotIn(forbidden, capture)
        self.assertIn("pcTaskGetName(NULL)", capture)

    def test_bluetooth_has_room_for_nested_bridge_and_hci_frames(self):
        manifest = (ROOT / "applications/services/bt/application.fam").read_text()
        self.assertIn("stack_size=2 * 1024", manifest)


if __name__ == "__main__":
    unittest.main()
