"""Native RTC OOM records, legacy-wire compatibility and crash-path ownership."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_crash_journal import RTC_STUB, HARNESS
from tools.tumoflip.test_subremote_lifecycle import function

ROOT = Path(__file__).resolve().parents[2]
STUB = RTC_STUB
PREAMBLE = HARNESS[:HARNESS.index("int main(void)")]


def native(code):
    with tempfile.TemporaryDirectory(prefix="oom-crash-test-") as directory:
        work = Path(directory)
        (work / "furi_hal_rtc.h").write_text(STUB)
        (work / "test.c").write_text(code)
        result = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(work), "-I", str(ROOT), "-I", str(ROOT / "lib"),
            str(work / "test.c"), str(ROOT / "furi/core/crash_journal.c"), "-o", str(work / "test"),
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(work / "test")], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)


class OomCrashContextTests(unittest.TestCase):
    def test_torn_writes_preserve_system_registers_and_never_validate_partial_record(self):
        native(PREAMBLE + r'''
int main(void){
 TumoflipOOMContext context={.requested=65536,.free_heap=187200,.largest_block=65528,
  .uptime_s=430000,.thread="DesktopS"};
 for(unsigned cut=0;cut<=13;cut++){
  memset(registers,0,sizeof(registers));
  for(unsigned reg=0;reg<8;reg++)registers[reg]=0xBEEF0000u+reg;
  writes_allowed=cut;
  tumoflip_crash_journal_record_oom("abcdef12",&context);
  writes_allowed=-1;
  for(unsigned reg=0;reg<8;reg++)assert(registers[reg]==0xBEEF0000u+reg);
  TumoflipCrashReport report;
  bool valid=tumoflip_crash_journal_read(&report);
  assert(valid==(cut>=13));
  if(valid){assert(report.has_oom_context&&report.oom.free_heap==187200);
    assert(report.oom.largest_block==65528&&report.oom.uptime_s==430000);}
 }
 return 0;
}
''')

    def test_uses_only_the_twenty_physical_backup_registers(self):
        native(PREAMBLE + r'''
_Static_assert(FuriHalRtcRegisterMAX<=20,"Hardware has only 20 backup registers");
int main(void){return 0;}
''')
    def test_wire_report_preserves_old_client_kind_and_complete_optional_details(self):
        formatter = function((ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c").read_text(),
                             "tumoflip_runtime_format_crash")
        native(PREAMBLE + "\n#include <stdio.h>\n" + formatter + r'''
int main(void){
 char output[192];
 tumoflip_runtime_format_crash(output,sizeof(output));
 assert(!strcmp(output,"schema=1;status=none"));
 TumoflipOOMContext context={.requested=UINT32_MAX,.free_heap=UINT32_MAX,
  .largest_block=UINT32_MAX,.uptime_s=0x0fffffff,.thread="BtSrv123"};
 tumoflip_crash_journal_record_oom("abcdef12",&context);
 tumoflip_runtime_format_crash(output,sizeof(output));
 // Existing Companion accepts only the old kind vocabulary, but ignores optional fields.
 assert(strstr(output,"kind=other;")&&strstr(output,"reason=oom;"));
 assert(strstr(output,"thread=BtSrv123;")&&strstr(output,"req=4294967295;"));
 assert(strstr(output,"free=4294967295;")&&strstr(output,"largest=4294967295;"));
 assert(strstr(output,"uptime_s=268435455"));
 assert(strlen(output)<sizeof(output)&&strlen(output)<=209);
 for(unsigned size=0;size<192;size++){
  unsigned char buffer[194];memset(buffer,0xA5,sizeof(buffer));
  tumoflip_runtime_format_crash((char*)&buffer[1],size);
  assert(buffer[0]==0xA5&&buffer[size+1]==0xA5);
  if(size){assert(memchr(&buffer[1],0,size));
   const char* s=(const char*)&buffer[1];
   if(strstr(s,"thread="))assert(strstr(s,"uptime_s=268435455"));
  }
 }
 tumoflip_crash_journal_record(TumoflipCrashKindNullPointer,"abcdef12");
 tumoflip_runtime_format_crash(output,sizeof(output));
 assert(strstr(output,"kind=null_pointer;")&&!strstr(output,"reason="));
 tumoflip_crash_journal_record(TumoflipCrashKindOutOfMemory,"abcdef12");
 tumoflip_runtime_format_crash(output,sizeof(output));
 assert(strstr(output,"reason=oom")&&!strstr(output,"req="));
 return 0;
}
''')

    def test_actual_oom_capture_uses_no_allocator_and_freezes_snapshot(self):
        capture = function((ROOT / "furi/core/check.c").read_text().replace("FURI_NORETURN ", ""),
                           "__furi_crash_oom")
        native(PREAMBLE + r'''
#include <setjmp.h>
#define configTICK_RATE_HZ 1000
static jmp_buf failed;
static bool irq_disabled;
static TumoflipOOMContext __furi_oom_context;
typedef struct{size_t xAvailableHeapSpaceInBytes,xSizeOfLargestFreeBlockInBytes;}HeapStats_t;
static void __disable_irq(void){irq_disabled=true;}
static void vPortGetHeapStats(HeapStats_t* s){assert(irq_disabled);s->xAvailableHeapSpaceInBytes=114672;s->xSizeOfLargestFreeBlockInBytes=113448;}
static uint32_t xTaskGetTickCount(void){return UINT32_MAX;}
static const char* pcTaskGetName(void* p){assert(!p);return "BtSrv;payload";}
#define furi_crash(message) do{assert(!strcmp(message,"out of memory"));longjmp(failed,1);}while(0)
''' + capture + r'''
int main(void){
 if(!setjmp(failed))__furi_crash_oom(123456);
 assert(__furi_oom_context.requested==123456&&__furi_oom_context.free_heap==114672);
 assert(__furi_oom_context.largest_block==113448&&__furi_oom_context.uptime_s==UINT32_MAX/1000);
 tumoflip_crash_journal_record_oom("abcdef12",&__furi_oom_context);
 TumoflipCrashReport report;assert(tumoflip_crash_journal_read(&report));
 assert(report.has_oom_context&&!strcmp(report.oom.thread,"BtSrv_pa"));return 0;
}
''')

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
                            .uptime_s=0x0fffffff,.thread="BtSrv;pa"};
 // A legacy record remains readable with no invented allocation context.
 tumoflip_crash_journal_record(TumoflipCrashKindOther,"8aaa6856");
 assert(tumoflip_crash_journal_read(&report)&&!report.has_oom_context);
 tumoflip_crash_journal_set_active_app("owned-app","ddfbeb36");
 tumoflip_crash_journal_record_oom("ddfbeb36",&context);
 assert(tumoflip_crash_journal_read(&report));
 assert(report.kind==TumoflipCrashKindOutOfMemory&&report.has_oom_context);
 assert(report.sequence==2&&!strcmp(report.app_id,"owned-app"));
 assert(report.oom.requested==UINT32_MAX&&report.oom.free_heap==114672);
 assert(report.oom.largest_block==113448&&report.oom.uptime_s==0x0fffffff);
 assert(!strcmp(report.oom.thread,"BtSrv_pa"));
 // Closing the popup/app must not erase the previous failure snapshot.
 tumoflip_crash_journal_clear_active_app();
 assert(tumoflip_crash_journal_read(&report)&&report.has_oom_context);
 tumoflip_crash_journal_set_active_app("new-app","ddfbeb36");
 assert(tumoflip_crash_journal_read(&report)&&report.has_oom_context);
 assert(!strcmp(report.app_id,"owned-app"));
 for(unsigned reg=FuriHalRtcRegisterTumoflipCrashMeta;reg<20;reg++){
  registers[reg]^=1;
  assert(!tumoflip_crash_journal_read(&report));
  registers[reg]^=1;
 }
 // A later legacy/non-OOM crash cannot inherit stale OOM details.
 tumoflip_crash_journal_record(TumoflipCrashKindWatchdog,"ddfbeb36");
 assert(tumoflip_crash_journal_read(&report)&&!report.has_oom_context);
 tumoflip_crash_journal_record_oom("ddfbeb36",NULL);
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
