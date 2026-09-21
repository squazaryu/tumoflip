"""Native iButton selection, legacy ABI and fail-closed persistence tests."""

import unittest
from tools.tumoflip.test_upstream_1435_native import PRELUDE, source
from tools.tumoflip.test_hotplug_assets import function, run_c


def body(path):
    return "\n".join(line for line in source(path).splitlines()
                     if not line.startswith(("#include", "#pragma once")))


TYPES = PRELUDE + "\ntypedef int OneWireHost;\n" + body("lib/ibutton/ibutton_write_targets.h")


class IButtonWriteTargetsNativeTests(unittest.TestCase):
    def test_masks_dispatch_only_selected_supported_targets(self):
        production = function(source("lib/ibutton/protocols/dallas/protocol_group_dallas.c"),
                              "static bool ibutton_protocol_group_dallas_write_id(")
        run_c(TYPES + r'''
typedef int iButtonProtocolLocalId;
typedef uint8_t iButtonProtocolData;
typedef struct {uint8_t* ptr;size_t size;} iButtonEditableData;
typedef struct {OneWireHost* host;} iButtonProtocolGroupDallas;
typedef struct {iButtonWriteTargetMask write_targets;void (*get_editable_data)(iButtonEditableData*,void*);} iButtonProtocolDallasBase;
#define iButtonProtocolDSMax 1
#define IBUTTON_ONEWIRE_ROM_SIZE 8
static int starts,stops,critical,announcements,attempts,winner=-1;
static uint32_t seen;
static void get_rom(iButtonEditableData* out,void* in) {out->ptr=in;out->size=8;}
static iButtonProtocolDallasBase descriptor={.get_editable_data=get_rom};
static const iButtonProtocolDallasBase* ibutton_protocols_dallas[]={&descriptor};
static void onewire_host_start(OneWireHost* h){(void)h;starts++;}
static void onewire_host_stop(OneWireHost* h){(void)h;assert(!critical);stops++;}
static void furi_delay_ms(int ms){(void)ms;assert(!critical);}
#define FURI_CRITICAL_ENTER() critical++
#define FURI_CRITICAL_EXIT() critical--
bool ibutton_write_target_write(OneWireHost* h,iButtonWriteTarget t,const uint8_t* p,size_t n) {
    (void)h;(void)p;assert(critical==1 && n==8);attempts++;seen|=1U<<t;return (int)t==winner;
}
static void announce(iButtonWriteTarget t,void* ctx){(void)t;(void)ctx;assert(!critical);announcements++;}
''' + production + r'''
int main(void) {
    (void)model_lock;OneWireHost host=0;iButtonProtocolGroupDallas group={&host};uint8_t data[8]={0};
    for(uint32_t supported=0;supported<16;supported++)for(uint32_t enabled=0;enabled<16;enabled++) {
        if(!supported)continue; // non-writable protocols never call this group method
        descriptor.write_targets=supported;seen=starts=stops=attempts=announcements=0;
        iButtonWriteTargetContext ctx={enabled,announce,NULL};
        assert(!ibutton_protocol_group_dallas_write_id(&group,data,0,&ctx));
        assert(seen==(supported&enabled));assert(attempts==announcements);
        assert(starts==!!seen && stops==!!seen && critical==0);
    }
    descriptor.write_targets=15;winner=1;seen=0;
    iButtonWriteTargetContext ctx={15,announce,NULL};
    assert(ibutton_protocol_group_dallas_write_id(&group,data,0,&ctx));assert(seen==3);
    return 0;
}
''')

    def test_settings_errors_disable_writes_without_replacing_the_file(self):
        text = source("lib/ibutton/ibutton_settings.c")
        declarations = body("lib/ibutton/ibutton_settings.c").split("iButtonWriteTargetMask ibutton_settings_get_write_targets(")[0]
        run_c(TYPES + r'''
#define EXT_PATH(p) p
#define RECORD_STORAGE 1
#define FURI_LOG_W(...) ((void)0)
typedef int Storage;
typedef int FS_Error;
enum {FSE_OK,FSE_NOT_EXIST,FSE_NOT_READY};
static int stat_result;
static bool load_ok;
static uint32_t persisted;
static Storage* furi_record_open(int r){(void)r;return (Storage*)1;}
static void furi_record_close(int r){(void)r;}
static FS_Error storage_common_stat(Storage* s,const char* p,void* i){(void)s;(void)p;(void)i;return stat_result;}
static bool saved_struct_load(const char* p,void* s,size_t n,int magic,int version){
    (void)p;(void)magic;(void)version;assert(n==sizeof(persisted));memcpy(s,&persisted,n);return load_ok;
}
iButtonWriteTargetMask ibutton_write_targets_default(void){return 15;}
''' + declarations + function(text, "iButtonWriteTargetMask ibutton_settings_get_write_targets(") + r'''
int main(void) {
    (void)model_lock;stat_result=FSE_NOT_EXIST;assert(ibutton_settings_get_write_targets()==15);
    stat_result=FSE_OK;load_ok=true;
    for(persisted=0;persisted<32;persisted++)assert(ibutton_settings_get_write_targets()==(persisted&15));
    load_ok=false;assert(ibutton_settings_get_write_targets()==0);
    stat_result=FSE_NOT_READY;assert(ibutton_settings_get_write_targets()==0);
    return 0;
}
''')

    def test_new_progress_is_opt_in_and_empty_mask_is_terminal(self):
        text = source("lib/ibutton/ibutton_worker_modes.c")
        funcs = "\n".join(function(text, signature) for signature in (
            "static void ibutton_worker_write_report(",
            "static void ibutton_worker_write_set_target(",
            "void ibutton_worker_mode_write_id_tick(iButtonWorker* worker) {",
            "void ibutton_worker_mode_write_copy_tick(iButtonWorker* worker) {",
        ))
        run_c(TYPES + r'''
enum {iButtonWorkerModeIdle,iButtonWorkerWriteOK,iButtonWorkerWriteNoDetect,iButtonWorkerWriteStartTarget,iButtonWorkerWriteNoEnabledTarget};
typedef int iButtonWorkerWriteResult;
typedef struct {void* key;void* protocols;void (*write_cb)(void*,int);void* cb_ctx;iButtonWriteTarget write_target;uint32_t write_target_mask;bool write_targets_configured;} iButtonWorker;
static int reports,last,delays,id_writes,copy_writes,idles;
static uint32_t supported=4;
static void report(void* ctx,int value){(void)ctx;reports++;last=value;}
static void furi_delay_ms(int ms){assert(ms==50);delays++;}
static uint32_t ibutton_protocols_get_write_targets(void* p,void* k){(void)p;(void)k;return supported;}
static void ibutton_worker_switch_mode(iButtonWorker* w,int mode){(void)w;assert(mode==iButtonWorkerModeIdle);idles++;}
static bool ibutton_protocols_write_id_targets(void* p,void* k,const iButtonWriteTargetContext* ctx){(void)p;(void)k;assert(ctx->mask&supported);id_writes++;return true;}
static bool ibutton_protocols_write_copy(void* p,void* k){(void)p;(void)k;copy_writes++;return true;}
''' + funcs + r'''
int main(void) {
    (void)model_lock;iButtonWorker worker={.key=(void*)1,.write_cb=report,.write_target_mask=15};
    ibutton_worker_write_set_target(0,&worker);assert(!reports && !delays);
    worker.write_targets_configured=true;
    ibutton_worker_write_set_target(1,&worker);assert(reports==1 && delays==1 && last==iButtonWorkerWriteStartTarget);
    worker.write_target_mask=0;ibutton_worker_mode_write_id_tick(&worker);
    assert(!id_writes && idles==1 && last==iButtonWorkerWriteNoEnabledTarget);
    worker.write_target_mask=1;ibutton_worker_mode_write_id_tick(&worker);
    assert(!id_writes && idles==2);
    worker.write_target_mask=4;ibutton_worker_mode_write_id_tick(&worker);assert(id_writes==1 && last==iButtonWorkerWriteOK);
    worker.write_target_mask=0;ibutton_worker_mode_write_copy_tick(&worker);
    assert(copy_writes==1 && last==iButtonWorkerWriteOK);
    return 0;
}
''')

    def test_public_signatures_and_old_enum_prefix_stay_compatible(self):
        header = source("lib/ibutton/ibutton_worker.h")
        old = ["iButtonWorkerWriteOK", "iButtonWorkerWriteSameKey", "iButtonWorkerWriteNoDetect", "iButtonWorkerWriteCannotWrite"]
        positions = [header.index(name) for name in old]
        self.assertEqual(positions, sorted(positions))
        self.assertLess(positions[-1], header.index("iButtonWorkerWriteStartTarget"))
        self.assertIn("bool ibutton_protocols_write_id(iButtonProtocols* protocols, iButtonKey* key)", source("lib/ibutton/ibutton_protocols.c"))


if __name__ == "__main__":
    unittest.main()
