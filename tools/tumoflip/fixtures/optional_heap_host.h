#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define PRIVILEGED_DATA
#define PRIVILEGED_FUNCTION
#define configENABLE_HEAP_PROTECTOR 1
#define configHEAP_CLEAR_MEMORY_ON_FREE 1
#ifndef configUSE_MALLOC_FAILED_HOOK
#define configUSE_MALLOC_FAILED_HOOK 0
#endif
#define portPOINTER_SIZE_TYPE uintptr_t
#define portBYTE_ALIGNMENT 8U
#define portBYTE_ALIGNMENT_MASK 7U
#define portMAX_DELAY SIZE_MAX
#define configASSERT assert
#define furi_assert assert
#define mtCOVERAGE_TEST_MARKER() ((void)0)
static _Alignas(8) uint8_t host_heap[187264];
#define configTOTAL_HEAP_SIZE (sizeof(host_heap))
#define SRAM_BASE ((uintptr_t)host_heap)
static jmp_buf fault;
static size_t failed_request;
static unsigned fatal_count,failed_hook_count,tracked_allocations,tracked_frees,depth;
static bool irq_mode;
static const char* failure_message;
#define FURI_IS_IRQ_MODE() irq_mode
#define furi_crash(message) do{failure_message=(message);fatal_count++;longjmp(fault,1);}while(0)
#define furi_check(value,message) do{if(!(value))furi_crash(message);}while(0)
static void __furi_crash_oom(size_t requested){failed_request=requested;furi_crash("out of memory");}
static void vTaskSuspendAll(void){depth++;}
static int xTaskResumeAll(void){assert(depth);depth--;return 0;}
#define taskENTER_CRITICAL() vTaskSuspendAll()
#define taskEXIT_CRITICAL() ((void)xTaskResumeAll())
static void memmgr_heap_init(void){}
static void traceMALLOC(void*p,size_t n){assert(depth);if(p){assert(n);tracked_allocations++;}}
static void traceFREE(void*p,size_t n){assert(depth&&p&&n);tracked_frees++;}
static void __attribute__((unused)) vApplicationMallocFailedHook(void){failed_hook_count++;}
void vApplicationGetRandomHeapCanary(uintptr_t* value){*value=(uintptr_t)0x13579BDFU;}
typedef struct{size_t xSizeOfLargestFreeBlockInBytes,xSizeOfSmallestFreeBlockInBytes,xNumberOfFreeBlocks,
 xAvailableHeapSpaceInBytes,xNumberOfSuccessfulAllocations,xNumberOfSuccessfulFrees,xMinimumEverFreeBytesRemaining;}HeapStats_t;
