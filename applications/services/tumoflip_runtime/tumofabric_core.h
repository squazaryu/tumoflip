#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOFABRIC_OWNER_MAX 16U

typedef struct {
    bool active;
    uint32_t session_id;
    uint32_t token;
    uint32_t last_sequence;
    int16_t counter;
    int8_t last_delta;
    char owner[TUMOFABRIC_OWNER_MAX + 1U];
} TumoFabricState;

typedef enum {
    TumoFabricResultOk,
    TumoFabricResultPayload,
    TumoFabricResultBusy,
    TumoFabricResultSession,
    TumoFabricResultSequence,
    TumoFabricResultRange,
} TumoFabricResult;

void tumofabric_init(TumoFabricState* state);
bool tumofabric_open_local(TumoFabricState* state);
bool tumofabric_step_local(TumoFabricState* state, int8_t delta);

TumoFabricResult tumofabric_open(
    TumoFabricState* state,
    uint32_t request_id,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size);

TumoFabricResult tumofabric_get_state(
    const TumoFabricState* state,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size);

TumoFabricResult tumofabric_step(
    TumoFabricState* state,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size);

TumoFabricResult tumofabric_cancel(
    TumoFabricState* state,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size);

const char* tumofabric_result_token(TumoFabricResult result);
