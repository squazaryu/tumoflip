#pragma once
#include "uhf_inventory.h"
#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>

typedef struct {
    UhfInventory* inventory;
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* stream;
    Expansion* expansion;
    uint32_t last_rx;
    uint32_t last_poll;
    uint32_t started;
    uint32_t completed;
    volatile bool overflow;
    bool initialized;
    bool running;
    bool timed_out;
} UhfRuntime;

bool uhf_runtime_connect(UhfRuntime* runtime, UhfInventory* inventory);
bool uhf_runtime_start(UhfRuntime* runtime);
void uhf_runtime_stop(UhfRuntime* runtime);
void uhf_runtime_tick(UhfRuntime* runtime);
void uhf_runtime_disconnect(UhfRuntime* runtime);
