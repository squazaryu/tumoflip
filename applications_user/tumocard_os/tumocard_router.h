#pragma once

#include "../tumovm_poc/tumovm_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOCARD_APPLET_MAX    4U
#define TUMOCARD_APPLET_ID_MAX 24U

typedef struct {
    char id[TUMOCARD_APPLET_ID_MAX];
    bool enabled;
    TumoVm vm;
} TumoCardApplet;

typedef struct {
    TumoCardApplet applets[TUMOCARD_APPLET_MAX];
    uint8_t applet_count;
    uint8_t invalid_count;
} TumoCardRegistry;

typedef struct {
    int8_t selected_index;
    TumoVmSession vm_session;
} TumoCardSession;

typedef enum {
    TumoCardAddResultOk,
    TumoCardAddResultFull,
    TumoCardAddResultInvalid,
    TumoCardAddResultDuplicateId,
    TumoCardAddResultDuplicateAid,
} TumoCardAddResult;

void tumocard_registry_init(TumoCardRegistry* registry);
TumoCardAddResult tumocard_registry_add(
    TumoCardRegistry* registry,
    const char* id,
    bool enabled,
    const TumoVmProgram* program,
    const uint8_t state[TUMOVM_STATE_MAX]);
bool tumocard_registry_set_enabled(TumoCardRegistry* registry, size_t index, bool enabled);

void tumocard_session_reset(TumoCardSession* session);
size_t tumocard_process_apdu(
    TumoCardRegistry* registry,
    TumoCardSession* session,
    TumoVmTransport transport,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t output_capacity);
