"""Native RTC OOM records, legacy-wire compatibility and crash-path ownership."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_crash_journal import RTC_STUB, HARNESS
from tools.tumoflip.test_subremote_lifecycle import function

ROOT = Path(__file__).resolve().parents[2]
STUB = RTC_STUB
PREAMBLE = HARNESS[:HARNESS.index("int main(void)")]


def native(code, portable_fallback=False):
    with tempfile.TemporaryDirectory(prefix="oom-crash-test-") as directory:
        work = Path(directory)
        (work / "furi_hal_rtc.h").write_text(STUB)
        (work / "test.c").write_text(code)
        result = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            *(["-fprofile-instr-generate", "-fcoverage-mapping"] if os.environ.get("TUMO_OOM_COVERAGE_DIR") else []),
            *(["-U__BYTE_ORDER__", "-D__BYTE_ORDER__=__ORDER_BIG_ENDIAN__"] if portable_fallback else []),
            "-I", str(work), "-I", str(ROOT), "-I", str(ROOT / "lib"),
            str(work / "test.c"), str(ROOT / "furi/core/crash_journal.c"), "-o", str(work / "test"),
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(work / "test")], capture_output=True, text=True,
                                env=dict(os.environ, LLVM_PROFILE_FILE=str(work / "test.profraw")))
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        if directory := os.environ.get("TUMO_OOM_COVERAGE_DIR"):
            target = Path(directory); target.mkdir(parents=True, exist_ok=True)
            number = len(list(target.glob("*.profraw")))
            shutil.copy2(work / "test.profraw", target / f"test-{number}.profraw")
            shutil.copy2(work / "test", target / "last-test")


class OomCrashContextTests(unittest.TestCase):
    @classmethod
    def tearDownClass(cls):
        if directory := os.environ.get("TUMO_OOM_COVERAGE_DIR"):
            target = Path(directory)
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", *map(str, target.glob("*.profraw")),
                            "-o", str(target / "total.profdata")], check=True, capture_output=True)
            report = json.loads(subprocess.check_output([
                "xcrun", "llvm-cov", "export", str(target / "last-test"),
                "-instr-profile=" + str(target / "total.profdata"), "-summary-only"], text=True))
            matched = [file for file in report["data"][0]["files"]
                       if Path(file["filename"]) == ROOT / "furi/core/crash_journal.c"]
            assert len(matched)==1
            summary = {key: round(matched[0]["summary"][key]["percent"],2) for key in ("lines","regions")}
            print("crash_journal production coverage:", summary)
            assert min(summary.values())>=80, "Crash journal coverage below 80%"

    def test_packed_heap_boundaries_and_all_legacy_kinds(self):
        native(PREAMBLE + r'''
int main(void){
 static const uint32_t values[]={0,1,65535,65536,131071,187264,262142,262143,UINT32_MAX};
 TumoflipCrashReport report;
 for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)
  for(unsigned j=0;j<sizeof(values)/sizeof(values[0]);j++){
   TumoflipOOMContext context={.requested=123,.free_heap=values[i],.largest_block=values[j],.uptime_s=UINT32_MAX};
   tumoflip_crash_journal_record_oom("abcdef12",&context);
   assert(tumoflip_crash_journal_read(&report)&&report.has_oom_context);
   assert(report.oom.free_heap==(values[i]>=262143?UINT32_MAX:values[i]));
   assert(report.oom.largest_block==(values[j]>=262143?UINT32_MAX:values[j]));
   assert(report.oom.uptime_s==0x0fffffff&&!report.oom.thread[0]);
  }
 const char* messages[]={"HardFault","NULL pointer dereference","MemManage 1","BusFault","UsageFault","StackOverflow","MPU fault 1"};
 for(unsigned i=0;i<sizeof(messages)/sizeof(messages[0]);i++){
  TumoflipCrashKind kind=tumoflip_crash_journal_classify(messages[i]);
  assert(kind!=TumoflipCrashKindOther);assert(strcmp(tumoflip_crash_journal_kind_name(kind),"none"));
 }
 for(unsigned kind=0;kind<=8;kind++)assert(tumoflip_crash_journal_kind_name((TumoflipCrashKind)kind));
 tumoflip_crash_journal_record(TumoflipCrashKindNone,NULL);
 tumoflip_crash_journal_record((TumoflipCrashKind)255,"bad");
 assert(!tumoflip_crash_journal_read(NULL));
 const char* hashes[]={NULL,"","123","abcdef1","ABcdeF12","bad!bad!","00000000"};
 for(unsigned i=0;i<sizeof(hashes)/sizeof(hashes[0]);i++){
  tumoflip_crash_journal_set_active_app(NULL,hashes[i]);
  tumoflip_crash_journal_record(TumoflipCrashKindOther,hashes[i]);
  assert(tumoflip_crash_journal_read(&report)&&!report.app_id[0]);
 }
 return 0;
}
''')

    def test_name_bytes_match_legacy_little_endian_register_format(self):
        code = PREAMBLE + r'''
int main(void){
 tumoflip_crash_journal_set_active_app("ABCDEFGHIJKL","abcdef12");
 tumoflip_crash_journal_record(TumoflipCrashKindOther,"abcdef12");
 assert(registers[FuriHalRtcRegisterTumoflipCrashApp0]==0x44434241);
 assert(registers[FuriHalRtcRegisterTumoflipCrashApp1]==0x48474645);
 assert(registers[FuriHalRtcRegisterTumoflipCrashApp2]==0x4c4b4a49);
 TumoflipCrashReport report;assert(tumoflip_crash_journal_read(&report));
 assert(!strcmp(report.app_id,"ABCDEFGHIJKL"));
 TumoflipOOMContext context={.requested=12345,.thread="BtSrv123"};
 tumoflip_crash_journal_record_oom("abcdef12",&context);
 assert(registers[FuriHalRtcRegisterTumoflipOOMThread0]==0x72537442);
 assert(registers[FuriHalRtcRegisterTumoflipOOMThread1]==0x33323176);
 assert(tumoflip_crash_journal_read(&report)&&!strcmp(report.oom.thread,"BtSrv123"));return 0;
}
'''
        native(code)
        native(code, portable_fallback=True)

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
static bool task_ready=true;
static TumoflipOOMContext __furi_oom_context;
typedef struct{size_t xAvailableHeapSpaceInBytes,xSizeOfLargestFreeBlockInBytes;}HeapStats_t;
static void __disable_irq(void){irq_disabled=true;}
static void vPortGetHeapStats(HeapStats_t* s){assert(irq_disabled);s->xAvailableHeapSpaceInBytes=114672;s->xSizeOfLargestFreeBlockInBytes=113448;}
static uint32_t xTaskGetTickCount(void){return UINT32_MAX;}
static void* xTaskGetCurrentTaskHandle(void){return task_ready?(void*)1:NULL;}
static const char* pcTaskGetName(void* p){assert(!p);return "BtSrv;payload";}
#define furi_crash(message) do{assert(!strcmp(message,"out of memory"));longjmp(failed,1);}while(0)
''' + capture + r'''
int main(void){
 if(!setjmp(failed))__furi_crash_oom(123456);
 assert(__furi_oom_context.requested==123456&&__furi_oom_context.free_heap==114672);
 assert(__furi_oom_context.largest_block==113448&&__furi_oom_context.uptime_s==UINT32_MAX/1000);
 tumoflip_crash_journal_record_oom("abcdef12",&__furi_oom_context);
 TumoflipCrashReport report;assert(tumoflip_crash_journal_read(&report));
 assert(report.has_oom_context&&!strcmp(report.oom.thread,"BtSrv_pa"));
 task_ready=false;
 if(!setjmp(failed))__furi_crash_oom(65536);
 assert(__furi_oom_context.requested==65536&&!strcmp(__furi_oom_context.thread,"main"));return 0;
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
