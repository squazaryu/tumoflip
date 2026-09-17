#pragma once
#include <furi.h>

typedef enum {
    P2sResultOk,
    P2sResultSkipped,
    P2sResultError,
    P2sResultCancelled,
} P2sResult;

typedef bool (*P2sCancelCallback)(void* context);

// Existing file data only: no protocol rewriting, decoding, or RF operation.
P2sResult p2s_convert_psf_to_sub(const char* path, P2sCancelCallback cancel, void* context);
P2sResult p2s_convert_sub_to_psf(const char* path, P2sCancelCallback cancel, void* context);
