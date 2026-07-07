#pragma once

#include <furi.h>

#define RECORD_TUMOFLIP_RUNTIME "tumoflip_runtime"

typedef struct TumoflipRuntimeApi TumoflipRuntimeApi;

struct TumoflipRuntimeApi {
    bool (*get_trace)(TumoflipRuntimeApi* api, char* output, size_t output_size);
    void* context;
};
