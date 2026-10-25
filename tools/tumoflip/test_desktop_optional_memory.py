"""Optional Desktop frame allocations must not turn heap fragmentation into a reset."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
STORAGE = ROOT / 'applications/services/desktop/animations/animation_storage.c'


class DesktopOptionalMemoryTests(unittest.TestCase):
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
 bool returned=false;
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
