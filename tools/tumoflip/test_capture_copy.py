"""Execute the production file-copy transaction with injected storage faults."""

from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class CaptureCopyTests(unittest.TestCase):
    def test_storage_faults_and_cancellation_preserve_original(self):
        path = ROOT / "applications/system/protopirate_to_subghz/copy_file.c"
        self.assertTrue(path.exists(), "A checked copy transaction is required")
        source = "\n".join(line for line in path.read_text().splitlines()
                           if not line.startswith("#include"))
        self.assertTrue("p2s_check_text" in source, "Bound field parsing before allocating strings")
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define P2S_MAX_FILE_BYTES (1024U * 1024U)
typedef enum {P2sResultOk, P2sResultSkipped, P2sResultError, P2sResultCancelled} P2sResult;
typedef bool (*P2sCancelCallback)(void*);
typedef int Storage;
enum {FSAM_READ, FSAM_WRITE, FSOM_OPEN_EXISTING, FSOM_CREATE_NEW, FSE_OK};
typedef struct {bool output, open; size_t pos;} File;
static unsigned char original[8192], saved[8192], output[8192];
static size_t original_size, output_size;
static int fault, allocated, open_files, syncs, removed, readbacks, writes, cancels;
static bool exists;
static File* storage_file_alloc(Storage* s) {(void)s; allocated++; return calloc(1,sizeof(File));}
static bool storage_file_close(File* f) {
    bool fail = f->open && ((fault==5 && f->output && !readbacks) || (fault==9 && !f->output));
    if(f->open) {f->open=false;open_files--;}
    return !fail;
}
static void storage_file_free(File* f) {assert(!f->open); allocated--;free(f);}
static bool storage_file_open(File* f,const char* path,int access,int mode) {
    assert(!f->open); f->output=!strcmp(path,"destination");f->pos=0;
    if(!f->output) {assert(access==FSAM_READ);if(fault==1)return false;}
    else if(mode==FSOM_CREATE_NEW) {
        assert(access==FSAM_WRITE);
        if(exists || fault==2)return false;
        exists=true;output_size=0;
    } else {assert(access==FSAM_READ);readbacks++;if(fault==6)return false;}
    f->open=true;open_files++;return true;
}
static uint64_t storage_file_size(File* f) {
    assert(f->open);return f->output?output_size:original_size;
}
static size_t storage_file_read(File* f,void* dst,size_t n) {
    assert(f->open);
    if(fault==3 && !f->output && f->pos>=512 && !readbacks)return 0;
    size_t size=f->output?output_size:original_size;
    n=MIN(n,size-f->pos);
    memcpy(dst,(f->output?output:original)+f->pos,n);f->pos+=n;
    if(fault==7 && f->output && n)((unsigned char*)dst)[0]^=1;
    return n;
}
static size_t storage_file_write(File* f,const void* src,size_t n) {
    assert(f->open && f->output);writes++;
    if(fault==4)n--;
    memcpy(output+f->pos,src,n);f->pos+=n;output_size=f->pos;return n;
}
static bool storage_file_seek(File* f,uint32_t pos,bool start) {
    assert(start);f->pos=pos;return fault!=8;
}
static bool storage_file_sync(File* f) {assert(f->open && f->output);syncs++;return fault!=10;}
static int storage_common_remove(Storage* s,const char* path) {
    (void)s;assert(!strcmp(path,"destination"));assert(!open_files);
    removed++;exists=false;return FSE_OK;
}
static bool cancelled(void* context) {
    (void)context;cancels++;
    return (fault==11) || (fault==12 && writes>=2) || (fault==13 && readbacks>0);
}
''' + source + r'''
int main(void) {
    for(fault=0;fault<=13;fault++) {
        memset(output,0,sizeof(output)); original_size=5000;output_size=0;
        for(size_t i=0;i<original_size;i++)original[i]=(unsigned char)(i%251);
        memcpy(saved,original,original_size);
        exists=false;allocated=open_files=syncs=removed=readbacks=writes=cancels=0;
        P2sResult r=p2s_copy_verified(NULL,"source","destination",cancelled,NULL);
        if(!fault) {
            assert(r==P2sResultOk && exists && syncs==1 && readbacks==1);
            assert(output_size==original_size && !memcmp(original,output,original_size));
        } else {
            assert(r==(fault>=11?P2sResultCancelled:P2sResultError));
            assert(!exists);
        }
        assert(!memcmp(original,saved,original_size));
        assert(allocated==0 && open_files==0);
    }
    fault=0;exists=false;original_size=2000;
    memset(original,'x',original_size);original[999]='\n';original[1999]='\n';
    assert(p2s_check_text(NULL,"source",NULL,NULL)==P2sResultOk);
    original[999]='x';
    assert(p2s_check_text(NULL,"source",NULL,NULL)==P2sResultSkipped);
    original[999]='\n';original[17]=0;
    assert(p2s_check_text(NULL,"source",NULL,NULL)==P2sResultSkipped);
    fault=1;assert(p2s_check_text(NULL,"source",NULL,NULL)==P2sResultError);
    fault=11;assert(p2s_check_text(NULL,"source",cancelled,NULL)==P2sResultCancelled);
    fault=0;exists=true;output_size=100;memset(output,0x7a,100);removed=0;
    assert(p2s_copy_verified(NULL,"source","destination",NULL,NULL)==P2sResultError);
    assert(exists && output_size==100 && output[0]==0x7a && removed==0);
    assert(allocated==0 && open_files==0);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
