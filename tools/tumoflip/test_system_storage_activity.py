"""Exercise actual storage API handle lifetimes against the core policy, no SD I/O."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"(?:static )?(?:bool|void|File\*|FS_Error)\s+" + name + r"\([^;]+?\)\s*\{", source)
    if not match:
        raise AssertionError(f"Missing function {name}")
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


STUBS = r"""
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "system_policy_i.h"
typedef int FS_AccessMode;
typedef int FS_OpenMode;
enum { FSAM_READ=1, FSAM_WRITE=2, FileTypeClosed, FileTypeOpenFile, FileTypeOpenDir,
       FSE_OK=0, FSE_NOT_READY=9, FSE_ALREADY_OPEN=10, StorageEventFlagFileClose=1,
       FuriFlagWaitAny=0, FuriWaitForever=0 };
typedef int Storage;
typedef int FuriEventFlag;
typedef int FuriPubSubSubscription;
typedef struct { int type, error_id; void* storage; bool write_tracked; } File;
static TumoSystemState state;
static unsigned opens, closes;
static bool succeed=true;
#define furi_check assert
#define FURI_LOG_T(...) ((void)0)
#define S_FILE_API_PROLOGUE assert(file)
#define S_API_PROLOGUE ((void)0)
#define S_API_DATA_FILE ((void)0)
#define S_API_MESSAGE(x) ((void)0)
#define S_API_EPILOGUE (closes++)
#define S_RETURN_BOOL true
#define storage_file_close_callback NULL
static bool furi_system_policy_acquire(TumoSystemActivity kind) {
    return tumo_system_model_acquire(&state,kind);
}
static void furi_system_policy_release(TumoSystemActivity kind) {
    tumo_system_model_release(&state,kind);
}
static FuriEventFlag* furi_event_flag_alloc(void) { static int event; return &event; }
static void* storage_get_pubsub(void* p) { return p; }
static void* furi_pubsub_subscribe(void* p,void* cb,void* event) { (void)cb;(void)event;return p; }
static void furi_pubsub_unsubscribe(void* p,void* s) { (void)p;(void)s; }
static void furi_event_flag_free(void* p) { (void)p; }
static void furi_event_flag_wait(void* p,int a,int b,int c) { (void)p;(void)a;(void)b;(void)c;assert(0); }
static bool storage_file_open_internal(File* file,const char* path,int access,int mode) {
    (void)path;(void)access;(void)mode;opens++;file->type=FileTypeOpenFile;
    file->error_id=succeed?FSE_OK:FSE_NOT_READY; return succeed;
}
static bool storage_file_is_open(File* f) { return f->type!=FileTypeClosed; }
static bool storage_file_is_dir(File* f) { return f->type==FileTypeOpenDir; }
static bool storage_dir_close(File* f) { f->type=FileTypeClosed; return true; }
"""

MAIN = r"""
int main(void) {
    Storage storage=1;
    tumo_system_model_init(&state);
    File* a=storage_file_alloc(&storage);
    assert(!a->write_tracked);
    assert(storage_file_open(a,"synthetic",FSAM_READ,0) && !state.writes);
    storage_file_close(a); assert(!state.writes);
    assert(storage_file_open(a,"synthetic",FSAM_WRITE,0) && state.writes==1);
    File* b=storage_file_alloc(&storage);
    assert(storage_file_open(b,"synthetic",FSAM_READ|FSAM_WRITE,0) && state.writes==2);
    assert(!storage_file_open(a,"synthetic",FSAM_WRITE,0) && state.writes==2);
    storage_file_free(b); assert(state.writes==1);
    storage_file_close(a); storage_file_close(a); assert(!state.writes);
    succeed=false;
    assert(!storage_file_open(a,"synthetic",FSAM_WRITE,0) && !state.writes);
    storage_file_close(a); assert(!state.writes && !a->write_tracked);
    state.recovery=true;
    assert(tumo_system_model_quiesce(&state,TumoSystemRestart,0,100));
    unsigned before=opens;
    assert(!storage_file_open(a,"synthetic",FSAM_WRITE,0));
    assert(opens==before && a->error_id==FSE_NOT_READY);
    storage_file_free(a);
    assert(closes>=4);
    return 0;
}
"""


class StorageActivityTests(unittest.TestCase):
    def test_metadata_and_media_commands_obey_the_same_barrier(self):
        source = (ROOT / "applications/services/storage/storage_external_api.c").read_text()
        extracted = "\n".join(function(source, name) for name in ("storage_path_command", "storage_sd_command"))
        stubs = STUBS[:STUBS.index("static FuriEventFlag*")]
        stubs = stubs.replace('static bool furi_system_policy_acquire', 'bool furi_system_policy_acquire')
        stubs = stubs.replace('static void furi_system_policy_release', 'void furi_system_policy_release')
        stubs += r'''
typedef int FS_Error;typedef int StorageCommand;
enum {StorageCommandCommonRemove,StorageCommandCommonMkDir,StorageCommandSDStatus,StorageCommandSDFormat};
typedef struct {struct{const char* path;int thread_id;}path;}SAData;
static int furi_thread_get_current_id(void){return 0;}
static unsigned queued;static StorageCommand current;
#undef S_API_MESSAGE
#undef S_API_EPILOGUE
#define S_API_MESSAGE(command) do{(void)data;current=command;}while(0)
#define S_API_EPILOGUE do{assert(state.writes==(current!=StorageCommandSDStatus));queued++;}while(0)
#define S_RETURN_ERROR FSE_OK
'''
        main = r'''
int main(void){
 (void)opens;(void)closes;(void)succeed;Storage storage=1;tumo_system_model_init(&state);
 assert(storage_path_command(&storage,"synthetic",StorageCommandCommonRemove)==FSE_OK&&!state.writes);
 assert(storage_sd_command(&storage,StorageCommandSDFormat)==FSE_OK&&!state.writes);
 assert(queued==2);state.recovery=true;assert(tumo_system_model_quiesce(&state,TumoSystemRestart,0,100));
 assert(storage_path_command(&storage,"synthetic",StorageCommandCommonMkDir)==FSE_NOT_READY);
 assert(storage_sd_command(&storage,StorageCommandSDFormat)==FSE_NOT_READY&&queued==2);
 assert(storage_sd_command(&storage,StorageCommandSDStatus)==FSE_OK&&queued==3);
 return 0;
}
'''
        self.native(stubs + extracted + main)

    def test_real_open_close_and_free_balance_write_leases(self):
        source = (ROOT / "applications/services/storage/storage_external_api.c").read_text()
        extracted = "\n".join(function(source, name) for name in (
            "storage_file_open", "storage_file_close", "storage_file_alloc", "storage_file_free"))
        # Private wrappers are replaced by synthetic state; the called production model is unchanged.
        stubs = STUBS.replace('static bool furi_system_policy_acquire', 'bool furi_system_policy_acquire')
        stubs = stubs.replace('static void furi_system_policy_release', 'void furi_system_policy_release')
        self.native(stubs + extracted + MAIN)

    def native(self, source):
        with tempfile.TemporaryDirectory(prefix="system-storage-") as temp:
            root = Path(temp)
            case = root / "case.c"
            case.write_text(source)
            result = subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-iquote", str(ROOT / "furi/core"), str(case),
                str(ROOT / "furi/core/system_policy_model.c"), "-o", str(root / "test"),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(root / "test")], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
