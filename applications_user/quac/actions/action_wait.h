#pragma once
#include <furi.h>

typedef struct App App;
typedef struct QuacActionWait QuacActionWait;

QuacActionWait* quac_action_wait_alloc(App* app);
// FuriWaitForever is reserved for RAW completion; timed actions validate first.
bool quac_action_wait_run(QuacActionWait* wait, uint32_t timeout_ms);
void quac_action_wait_complete(void* context);
// Call only after the operation and any completion callbacks have stopped.
void quac_action_wait_free(QuacActionWait* wait);
