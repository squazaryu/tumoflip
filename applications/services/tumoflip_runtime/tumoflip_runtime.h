#pragma once

#include <furi.h>

#define RECORD_TUMOFLIP_RUNTIME "tumoflip_runtime"

typedef struct TumoflipRuntimeApi TumoflipRuntimeApi;

#define TUMOFLIP_RUNTIME_FABRIC_OWNER_MAX 16U

typedef struct {
    bool active;
    uint32_t session_id;
    uint32_t token;
    uint32_t last_sequence;
    int16_t counter;
    char owner[TUMOFLIP_RUNTIME_FABRIC_OWNER_MAX + 1U];
} TumoflipRuntimeFabricSnapshot;

struct TumoflipRuntimeApi {
    bool (*get_trace)(TumoflipRuntimeApi* api, char* output, size_t output_size);
    bool (*get_fabric_state)(
        TumoflipRuntimeApi* api,
        TumoflipRuntimeFabricSnapshot* snapshot);
    bool (*cancel_fabric)(TumoflipRuntimeApi* api);
    bool (*open_local_fabric)(TumoflipRuntimeApi* api);
    bool (*step_local_fabric)(TumoflipRuntimeApi* api, int8_t delta);
    void* context;
};
