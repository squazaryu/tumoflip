"""Actual FreeRTOS heap search/split/coalesce with recoverable optional allocations."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


def heap_source(hook=0):
    source = (ROOT / 'furi/core/memmgr_heap.c').read_text()
    block = source[source.index('/* Block sizes must not get too small. */'):source.index('/* Assert that a heap block pointer')]
    block = block.replace('uint8_t* ucHeap = (uint8_t*)&__heap_start__;', 'static uint8_t* ucHeap = host_heap;')
    globals_ = source[source.index('static const size_t xHeapStructSize'):source.index('/* Furi heap extension */')]
    prefix = (ROOT / 'tools/tumoflip/fixtures/optional_heap_host.h').read_text()
    validation = '#define heapVALIDATE_BLOCK_POINTER(p) assert((uint8_t*)(p)>=ucHeap&&(uint8_t*)(p)<ucHeap+configTOTAL_HEAP_SIZE)\n'
    definitions = '\n'.join(function(source, signature) for signature in (
        'static void prvHeapInit(void) /* PRIVILEGED_FUNCTION */',
        'static void prvInsertBlockIntoFreeList(BlockLink_t* pxBlockToInsert) /* PRIVILEGED_FUNCTION */',
    ))
    optional = function(source, 'void* memmgr_heap_alloc_optional(') if 'void* memmgr_heap_alloc_optional(' in source else ''
    declarations = 'static void prvHeapInit(void);static void prvInsertBlockIntoFreeList(BlockLink_t*);\nvoid* memmgr_heap_alloc_optional(size_t);\n'
    body = '\n'.join(function(source, signature) for signature in (
        'void* pvPortMalloc(', 'void vPortFree(', 'void vPortGetHeapStats(',
    ))
    return f'#define configUSE_MALLOC_FAILED_HOOK {hook}\n' + prefix + block + validation + globals_ + declarations + definitions + optional + body + r'''
static void __attribute__((unused)) fragmented(void){
 memset(host_heap,0,sizeof(host_heap));prvHeapInit();
 const size_t offsets[]={0,40000,80000,120000},sizes[]={31784,30000,25000,13664};
 for(unsigned i=0;i<4;i++){
  BlockLink_t*b=(BlockLink_t*)(host_heap+offsets[i]);b->xBlockSize=sizes[i];
  b->pxNextFreeBlock=heapPROTECT_BLOCK_POINTER(i<3?(BlockLink_t*)(host_heap+offsets[i+1]):pxEnd);
 }
 xStart.pxNextFreeBlock=heapPROTECT_BLOCK_POINTER((BlockLink_t*)host_heap);
 xFreeBytesRemaining=xMinimumEverFreeBytesRemaining=100448;
 xNumberOfSuccessfulAllocations=xNumberOfSuccessfulFrees=0;
 fatal_count=failed_hook_count=tracked_allocations=tracked_frees=depth=0;failure_message=NULL;failed_request=0;
}
'''


class OptionalHeapTests(unittest.TestCase):
    def test_cold_initialisation_exact_fit_and_coalescing_remain_usable(self):
        native(heap_source() + r'''
int main(void){
 memset(host_heap,0xA5,sizeof(host_heap));pxEnd=NULL;
 void*a=memmgr_heap_alloc_optional(17);assert(a&&!depth);
 for(unsigned i=0;i<17;i++)assert(!((uint8_t*)a)[i]);
 void*b=memmgr_heap_alloc_optional(79),*c=memmgr_heap_alloc_optional(37);assert(b&&c);
 vPortFree(a);vPortFree(c);vPortFree(b);
 HeapStats_t full;vPortGetHeapStats(&full);assert(full.xNumberOfFreeBlocks==1);
 size_t available=full.xAvailableHeapSpaceInBytes;
 a=memmgr_heap_alloc_optional(available-xHeapStructSize);assert(a&&xFreeBytesRemaining==0);
 assert(!memmgr_heap_alloc_optional(1)&&!fatal_count);
 vPortFree(a);assert(xFreeBytesRemaining==available);
 assert(!memmgr_heap_alloc_optional(SIZE_MAX-xHeapStructSize-3));
 assert(!memmgr_heap_alloc_optional((SIZE_MAX/2)+1));
 assert(!depth&&!fatal_count);return 0;
}
''')

    def test_mandatory_allocator_still_crashes_and_preserves_request(self):
        native(heap_source() + r'''
int main(void){
 fragmented();HeapStats_t stats;vPortGetHeapStats(&stats);
 assert(stats.xAvailableHeapSpaceInBytes==100448&&stats.xSizeOfLargestFreeBlockInBytes==31784);
 if(!setjmp(fault)){pvPortMalloc(35268);assert(!"mandatory OOM must stay fatal");}
 assert(fatal_count==1&&failed_request==35268&&!depth);
 if(!setjmp(fault)){pvPortMalloc(0);assert(!"malloc(0) must stay invalid");}
 assert(!strcmp(failure_message,"malloc(0)"));
 if(!setjmp(fault)){pvPortMalloc(SIZE_MAX);assert(!"overflow must preserve the original request");}
 assert(failed_request==SIZE_MAX&&!depth);
 void*p=pvPortMalloc(71);assert(p&&!((uintptr_t)p&7));for(unsigned i=0;i<71;i++)assert(!((uint8_t*)p)[i]);
 memset(p,0xA5,71);vPortFree(p);assert(xFreeBytesRemaining==100448&&tracked_frees==1&&!depth);
 irq_mode=true;if(!setjmp(fault)){pvPortMalloc(1);assert(!"ISR allocation must remain invalid");}
 assert(!strcmp(failure_message,"memmgt in ISR"));return 0;
}
''')

    def test_optional_allocation_is_atomic_nonfatal_and_does_not_call_failure_hook(self):
        native(heap_source(hook=1) + r'''
int main(void){
 fragmented();HeapStats_t before,after;vPortGetHeapStats(&before);
 assert(memmgr_heap_alloc_optional(35268)==NULL);
 assert(memmgr_heap_alloc_optional(0)==NULL&&memmgr_heap_alloc_optional(SIZE_MAX)==NULL);
 assert(!fatal_count&&!failed_hook_count&&!tracked_allocations&&!depth);
 vPortGetHeapStats(&after);assert(!memcmp(&before,&after,sizeof(before)));
 void*a=memmgr_heap_alloc_optional(252),*b=memmgr_heap_alloc_optional(1025);assert(a&&b&&tracked_allocations==2);
 for(unsigned i=0;i<252;i++)assert(!((uint8_t*)a)[i]);
 vPortFree(a);vPortFree(b);vPortGetHeapStats(&after);
 assert(after.xAvailableHeapSpaceInBytes==100448&&after.xSizeOfLargestFreeBlockInBytes==31784);
 if(!setjmp(fault)){pvPortMalloc(35268);assert(!"mandatory failure must still be reported");}
 assert(failed_hook_count==1&&fatal_count==1&&failed_request==35268);
 irq_mode=true;if(!setjmp(fault)){memmgr_heap_alloc_optional(1);assert(!"optional is not ISR-safe");}
 assert(!strcmp(failure_message,"memmgt in ISR"));return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
