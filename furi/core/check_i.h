#pragma once

#include <stddef.h>
#include "common_defines.h"

/* Private allocator failure path; no SDK export and no allocation/SD I/O. */
FURI_NORETURN void __furi_crash_oom(size_t requested);
