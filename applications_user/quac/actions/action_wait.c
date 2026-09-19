#include "action_wait.h"
#include "quac.h"
#include <input/input.h>

#define QUAC_WAIT_CANCEL (1U << 0)
#define QUAC_WAIT_DONE   (1U << 1)

struct QuacActionWait {
    App* app;
    FuriEventFlag* flags;
    FuriPubSub* input;
    FuriPubSubSubscription* subscription;
};

static void quac_action_wait_input(const void* value, void* context) {
    QuacActionWait* wait = context;
    const InputEvent* event = value;
    if(event->key == InputKeyBack && event->type == InputTypePress)
        furi_event_flag_set(wait->flags, QUAC_WAIT_CANCEL);
}

QuacActionWait* quac_action_wait_alloc(App* app) {
    QuacActionWait* wait = malloc(sizeof(QuacActionWait));
    wait->app = app;
    wait->flags = furi_event_flag_alloc();
    wait->input = furi_record_open(RECORD_INPUT_EVENTS);
    wait->subscription = furi_pubsub_subscribe(wait->input, quac_action_wait_input, wait);
    return wait;
}

bool quac_action_wait_run(QuacActionWait* wait, uint32_t timeout_ms) {
    const uint32_t timeout = timeout_ms == FuriWaitForever ? FuriWaitForever :
                                                             furi_ms_to_ticks(timeout_ms);
    const uint32_t flags = furi_event_flag_wait(
        wait->flags, QUAC_WAIT_CANCEL | QUAC_WAIT_DONE, FuriFlagWaitAny, timeout);
    if(flags == (uint32_t)FuriFlagErrorTimeout) return true;
    if(flags & FuriFlagError) return false;
    if(flags & QUAC_WAIT_CANCEL) {
        wait->app->action_cancelled = true;
        wait->app->suppress_next_back = true;
        return false;
    }
    return (flags & QUAC_WAIT_DONE) != 0;
}

void quac_action_wait_complete(void* context) {
    QuacActionWait* wait = context;
    furi_event_flag_set(wait->flags, QUAC_WAIT_DONE);
}

void quac_action_wait_free(QuacActionWait* wait) {
    furi_pubsub_unsubscribe(wait->input, wait->subscription);
    furi_record_close(RECORD_INPUT_EVENTS);
    furi_event_flag_free(wait->flags);
    free(wait);
}
