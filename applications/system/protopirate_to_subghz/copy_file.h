#pragma once

#include <storage/storage.h>
#include "converter.h"

#define P2S_MAX_FILE_BYTES (1024U * 1024U)

P2sResult
    p2s_check_text(Storage* storage, const char* source, P2sCancelCallback cancel, void* context);

P2sResult p2s_copy_verified(
    Storage* storage,
    const char* source,
    const char* destination,
    P2sCancelCallback cancel,
    void* context);
