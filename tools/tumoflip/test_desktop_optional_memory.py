"""Optional Desktop frame allocations must not turn heap fragmentation into a reset."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
STORAGE = ROOT / 'applications/services/desktop/animations/animation_storage.c'


def storage_native(program, metadata=False, manager=False):
    source = STORAGE.read_text()
    structures = source[source.index('typedef struct {'):source.index('_Static_assert')]
    signatures = ('static void animation_storage_free_frames(BubbleAnimation* animation) {',
                  'static bool animation_storage_prepare_icon(',
                  'static bool animation_storage_frame_data_is_valid(',
                  'static bool animation_storage_load_frame_bundle(',
                  'static bool animation_storage_load_frames(')
    body = '#include "desktop_oom_io.h"\n' + structures + '\n#define malloc frame_malloc\n#define calloc frame_calloc\n#define free frame_free\n'
    body += '\n'.join(function(source, signature) for signature in signatures)
    if metadata or manager:
        body += '\n#define furi_record_open animation_record_open\n'
        body += function(source, 'static void animation_storage_free_animation(BubbleAnimation** animation) {')
        body += function(source, 'static BubbleAnimation* animation_storage_load_animation(const char* name) {')
    if manager:
        body += '\n#include "desktop_oom_manager.h"\n'
        body += function(source, 'void animation_storage_cache_animation(')
        body += function(source, 'const BubbleAnimation*\n    animation_storage_get_bubble_animation(')
        manager_source = (ROOT / 'applications/services/desktop/animations/animation_manager.c').read_text()
        body += function(manager_source, 'static StorageAnimation*\n    animation_manager_select_idle_animation(AnimationManager* animation_manager) {')
    body += '\n#undef malloc\n#undef calloc\n#undef free\n' + program
    with tempfile.TemporaryDirectory(prefix='desktop-oom-io-') as directory:
        work = Path(directory); (work / 'sd').mkdir(); (work / 'test.c').write_text(body)
        command = ['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-DMBEDTLS_CONFIG_FILE="library_crypto_config.h"',
                   '-I', str(ROOT / 'tools/tumoflip/fixtures'), '-I', str(ROOT / 'lib/mbedtls/include'),
                   str(work / 'test.c'), '-o', str(work / 'test')]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode: raise AssertionError(result.stderr)
        result = subprocess.run([str(work / 'test'), str(work / 'sd')], capture_output=True, text=True)
        if result.returncode: raise AssertionError(result.stderr)


class DesktopOptionalMemoryTests(unittest.TestCase):
    def test_actual_idle_selector_uses_builtin_after_failed_optional_frames(self):
        storage_native(r'''
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};setup_storage(&s,argv[1]);metadata_storage=&s;small_bundle(&s);
 AnimationManager manager={0};allocation_fail_at=4;
 StorageAnimation*selected=animation_manager_select_idle_animation(&manager);
 assert(selected==&builtin&&builtin_selections==1&&discarded_candidates==1);
 assert(!candidate.animation&&!live_frames&&!fatal_allocations);
 allocation_fail_at=-1;allocation_calls=0;
 selected=animation_manager_select_idle_animation(&manager);
 assert(selected==&candidate&&selected->animation&&live_frames==3);
 assert(builtin_selections==1);animation_storage_free_storage_animation(&selected);
 assert(!selected&&!live_frames);return 0;
}
''', manager=True)

    def test_metadata_failure_after_frames_does_not_leak_the_loaded_bundle(self):
        storage_native(r'''
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};setup_storage(&s,argv[1]);metadata_storage=&s;small_bundle(&s);
 metadata_fail_after_frames=true;
 BubbleAnimation*a=animation_storage_load_animation("Fixture");assert(!a);
 assert(!live_frames&&!fatal_allocations);
 metadata_fail_after_frames=false;allocation_calls=0;
 a=animation_storage_load_animation("Fixture");assert(a&&a->icon_animation.frames&&live_frames==3);
 animation_storage_free_animation(&a);assert(!a&&!live_frames);
 allocation_calls=0;allocation_fail_at=4;
 a=animation_storage_load_animation("Fixture");assert(!a&&!live_frames&&!fatal_allocations);
 return 0;
}
''', metadata=True)

    def test_bundle_load_and_corrupt_or_partial_reads_release_every_frame(self):
        storage_native(r'''
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};setup_storage(&s,argv[1]);small_bundle(&s);
 BubbleAnimation a={.passive_frames=1,.active_frames=1};uint32_t order[]={0,1};
 assert(animation_storage_load_frames(&s,"Fixture",&a,order,32,2));
 assert(live_frames==1&&a.icon_animation.frame_count==2);
 assert(a.icon_animation.frames[0][1]==0xaa&&a.icon_animation.frames[1][4]==0xbb);
 animation_storage_free_frames(&a);assert(!live_frames&&!a.icon_animation.frames);
 for(unsigned mode=0;mode<9;mode++){
  uint8_t invalid[]={ 'T','F','A','B',15,0,0,0,1,2,32,2, 9,0,6,0,
   0,0xaa,0,0,0,0,0,0,0, 1,0,2,0,0xbb,0xcc };
  const unsigned positions[]={0,8,9,10,11,4,12,16,27};
  invalid[positions[mode]]^=1;blob(&s,ANIMATION_DIR "/Fixture/frames.bin",invalid,sizeof(invalid));
  assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));
  assert(!live_frames&&!a.icon_animation.frames&&!fatal_allocations);
 }
 small_bundle(&s);allocation_calls=0;allocation_fail_at=1;
 assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));assert(!live_frames&&!fatal_allocations);
 allocation_fail_at=-1;largest_allowed=0;
 assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));assert(!live_frames&&!fatal_allocations);
 largest_allowed=SIZE_MAX;
 for(unsigned n=0;n<31;n++){
  read_budget=n;bool ok=animation_storage_load_frames(&s,"Fixture",&a,order,32,2);assert(!ok);
  assert(!live_frames&&!fatal_allocations&&!a.icon_animation.frames);read_budget=-1;
 }
 const uint8_t bad[]={ 'T','F','A','B',15,0,0,0,1,2,32,2, 0,0,6,0,
  0,0xaa,0,0,0,0,0,0,0, 1,0,2,0,0xbb,0xcc };
 blob(&s,ANIMATION_DIR "/Fixture/frames.bin",bad,sizeof(bad));
 assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));assert(!live_frames&&!a.icon_animation.frames);
 order[1]=UINT32_MAX;assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));
 a.passive_frames=a.active_frames=0;assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));
 return 0;
}
''')

    def test_failed_legacy_frame_allocation_is_nonfatal_and_keeps_cleanup_complete(self):
        storage_native(r'''
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};setup_storage(&s,argv[1]);
 const uint8_t raw[]={0,0xaa,0,0,0,0,0,0,0},compressed[]={1,0,2,0,0xbb,0xcc};
 blob(&s,ANIMATION_DIR "/Fixture/frame_0.bm",raw,sizeof(raw));
 BubbleAnimation a={.passive_frames=1,.active_frames=1};uint32_t order[]={0,1};
 assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));assert(!live_frames&&!fatal_allocations);
 blob(&s,ANIMATION_DIR "/Fixture/frame_1.bm",compressed,sizeof(compressed));allocation_calls=0;
 volatile bool returned=false;allocation_fail_at=3; // Header + first frame succeed, second frame fails.
 if(!setjmp(allocation_fault)){assert(!animation_storage_load_frames(&s,"Fixture",&a,order,32,2));returned=true;}
 assert(returned&&!fatal_allocations&&!live_frames&&!a.icon_animation.frames);
 allocation_fail_at=-1;allocation_calls=0;
 assert(animation_storage_load_frames(&s,"Fixture",&a,order,32,2));assert(live_frames==3);
 animation_storage_free_frames(&a);assert(!live_frames);return 0;
}
''')

    def test_fragmented_heap_rejects_optional_bundle_without_a_fatal_allocation(self):
        source = STORAGE.read_text()
        owner = re.search(r'typedef struct \{\n    const uint8_t\* bundled_data;.*?\} AnimationFrameStorage;', source, re.S)
        self.assertIsNotNone(owner)
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#define furi_assert assert
#define FURI_CONST_ASSIGN(dst,value) ((dst)=(value))
#define FURI_CONST_ASSIGN_PTR(dst,value) ((dst)=(value))
typedef struct{uint8_t frame_count,frame_rate,height,width;const uint8_t**frames;}Icon;
typedef struct{Icon icon_animation;}BubbleAnimation;
static jmp_buf fatal;
static size_t largest=31784,requested;
static unsigned fatal_calls,allocations;
static void* host_optional(size_t size){requested=size;if(size>largest)return NULL;allocations++;return calloc(1,size);}
static void* __attribute__((unused)) memmgr_heap_alloc_optional(size_t size){return host_optional(size);}
static void* __attribute__((unused)) fatal_calloc(size_t n,size_t size){
 void*p=host_optional(n*size);if(!p){fatal_calls++;longjmp(fatal,1);}return p;
}
#define calloc fatal_calloc
''' + owner.group(0) + '\n' + function(source, 'static bool animation_storage_prepare_icon(') + r'''
#undef calloc
int main(void){
 BubbleAnimation animation={0};
 volatile bool returned=false;
 if(!setjmp(fatal)){
  assert(!animation_storage_prepare_icon(&animation,62,128,64,35016));returned=true;
 }
 assert(returned&&!fatal_calls&&!animation.icon_animation.frames&&!allocations);
 assert(requested==sizeof(AnimationFrameStorage)+62*sizeof(const uint8_t*)+35016);
 assert(4+62*4+35016==35268); // Exact physical F7 request from the retained record.
 largest=100448;
 assert(animation_storage_prepare_icon(&animation,62,128,64,35016));
 assert(animation.icon_animation.frame_count==62&&animation.icon_animation.width==128);
 assert(animation.icon_animation.height==64&&animation.icon_animation.frame_rate==0);
 AnimationFrameStorage*data=(AnimationFrameStorage*)((uint8_t*)animation.icon_animation.frames - offsetof(AnimationFrameStorage,frames));
 assert(data->bundled_data==(const uint8_t*)data->frames+62*sizeof(const uint8_t*));
 for(unsigned i=0;i<62;i++)assert(!animation.icon_animation.frames[i]);
 free(data);return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
