#include "system_policy_i.h"
#include <string.h>

static bool expired(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

void tumo_system_model_init(TumoSystemState* state) {
    memset(state, 0, sizeof(*state));
    state->display_on = true;
    state->phone_allowed = true;
}

static uint16_t* counter(TumoSystemState* state, TumoSystemActivity activity) {
    switch(activity) {
    case TumoSystemWrite:
        return &state->writes;
    case TumoSystemPreview:
        return &state->previews;
    case TumoSystemPhonePreview:
        return &state->phone_previews;
    default:
        return NULL;
    }
}

bool tumo_system_model_acquire(TumoSystemState* state, TumoSystemActivity activity) {
    uint16_t* count = counter(state, activity);
    if(!count || *count == 255 || state->closing ||
       (activity == TumoSystemPhonePreview && !state->phone_allowed))
        return false;
    ++*count;
    return true;
}

void tumo_system_model_release(TumoSystemState* state, TumoSystemActivity activity) {
    uint16_t* count = counter(state, activity);
    if(count && *count) --*count;
}

bool tumo_system_model_transfer(TumoSystemState* state, bool active, uint32_t now, uint32_t lease) {
    if(active && state->closing) return false;
    state->transfer = active;
    state->transfer_until = now + lease;
    return true;
}

void tumo_system_model_expire(TumoSystemState* state, uint32_t now) {
    if(state->transfer && expired(now, state->transfer_until)) state->transfer = false;
    if(state->closing == TumoSystemPhoneDisconnect &&
       (!state->phone_previews || expired(now, state->closing_until)))
        state->closing = TumoSystemNotClosing;
}

bool tumo_system_model_busy(TumoSystemState* state, uint32_t now) {
    tumo_system_model_expire(state, now);
    return state->writes || state->foreground || state->transfer || state->closing;
}

bool tumo_system_model_quiet(const TumoSystemState* state) {
    return state->recovery || (!state->display_on && !state->previews && !state->phone_previews);
}

bool tumo_system_model_quiesce(
    TumoSystemState* state,
    TumoSystemBarrier reason,
    uint32_t now,
    uint32_t timeout) {
    if((reason != TumoSystemPhoneDisconnect && reason != TumoSystemRestart) ||
       tumo_system_model_busy(state, now) || (reason == TumoSystemRestart && !state->recovery) ||
       (reason == TumoSystemPhoneDisconnect && !state->phone_previews))
        return false;
    state->closing = reason;
    state->closing_until = now + timeout;
    if(reason == TumoSystemPhoneDisconnect) state->phone_allowed = false;
    return true;
}
