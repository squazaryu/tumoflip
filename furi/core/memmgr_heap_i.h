#pragma once

#include <stddef.h>

/* Private resident-only allocator for optional resources. An unsatisfied request
 * returns NULL instead of recording OOM or invoking the malloc-failed hook. A
 * successful allocation is zeroed. Free
 * successful allocations normally. Like malloc, this is forbidden in an ISR.
 * This is not an SDK export; mandatory allocations must continue to use malloc.
 */
void* memmgr_heap_alloc_optional(size_t size);
