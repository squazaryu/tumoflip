#include "system_policy_i.h"
#include <furi.h>
#include <furi_hal_rtc.h>

static TumoSystemState policy;
static bool initialized, healthy;
static uint32_t started;
static void (*listener)(void*);
static void* listener_context;

static void ensure_initialized(void) {
    if(initialized) return;
    tumo_system_model_init(&policy);
    policy.boot_crashes = tumoflip_crash_journal_boot_streak();
    policy.recovery = policy.boot_crashes >= 3;
    started = furi_get_tick();
    initialized = true;
}

static uint32_t signature(void) {
    uint32_t flags = policy.display_on | (policy.foreground << 1U) | (policy.transfer << 2U) |
                     (policy.phone_allowed << 3U) | (policy.recovery << 4U) |
                     ((uint32_t)policy.closing << 5U);
    return policy.writes | ((uint32_t)policy.previews << 8U) |
           ((uint32_t)policy.phone_previews << 16U) | (flags << 24U);
}

static void notify(uint32_t before) {
    void (*callback)(void*) = NULL;
    void* context = NULL;
    FURI_CRITICAL_ENTER();
    if(before != signature()) {
        callback = listener;
        context = listener_context;
    }
    FURI_CRITICAL_EXIT();
    if(callback) callback(context);
}

TumoSystemState furi_system_policy_snapshot(void) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    const uint32_t now = furi_get_tick();
    tumo_system_model_expire(&policy, now);
    if(!healthy && (now - started) >= furi_ms_to_ticks(60000)) {
        healthy = true;
        tumoflip_crash_journal_clear_boot_streak();
    }
    TumoSystemState snapshot = policy;
    FURI_CRITICAL_EXIT();
    notify(before);
    return snapshot;
}

typedef enum {
    PolicyAcquire,
    PolicyRelease,
    PolicyTransfer,
    PolicyApplication,
    PolicyDisplay,
    PolicyAllowPreview,
    PolicyQuiesce,
} PolicyChange;

/* Share the transaction boundary, including exactly one out-of-lock notification.
 * Keeping this out of line avoids seven copies of the same core bookkeeping. */
static __attribute__((noinline)) bool policy_change(PolicyChange change, uint32_t value) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    bool result = true;
    switch(change) {
    case PolicyAcquire:
        tumo_system_model_expire(&policy, furi_get_tick());
        result = tumo_system_model_acquire(&policy, value);
        break;
    case PolicyRelease:
        tumo_system_model_release(&policy, value);
        break;
    case PolicyTransfer:
        result =
            tumo_system_model_transfer(&policy, value, furi_get_tick(), furi_ms_to_ticks(30000));
        break;
    case PolicyApplication:
        policy.foreground = value;
        break;
    case PolicyDisplay:
        policy.display_on = value;
        break;
    case PolicyAllowPreview:
        tumo_system_model_expire(&policy, furi_get_tick());
        result = policy.closing == TumoSystemNotClosing;
        if(result) policy.phone_allowed = true;
        break;
    case PolicyQuiesce:
        result =
            tumo_system_model_quiesce(&policy, value, furi_get_tick(), furi_ms_to_ticks(5000));
        if(result && value == TumoSystemRestart) tumoflip_crash_journal_clear_boot_streak();
        break;
    }
    FURI_CRITICAL_EXIT();
    notify(before);
    return result;
}

bool furi_system_policy_acquire(TumoSystemActivity activity) {
    return policy_change(PolicyAcquire, activity);
}

void furi_system_policy_release(TumoSystemActivity activity) {
    policy_change(PolicyRelease, activity);
}

bool furi_system_policy_transfer(bool active) {
    return policy_change(PolicyTransfer, active);
}

void furi_system_policy_application(bool active) {
    policy_change(PolicyApplication, active);
}

void furi_system_policy_display(bool on) {
    policy_change(PolicyDisplay, on);
}

void furi_system_policy_listen(void (*callback)(void*), void* context) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    listener = callback;
    listener_context = context;
    FURI_CRITICAL_EXIT();
}

bool furi_system_policy_pause_phone_preview(void) {
    return policy_change(PolicyQuiesce, TumoSystemPhoneDisconnect);
}

bool furi_system_policy_prepare_normal_restart(void) {
    return policy_change(PolicyQuiesce, TumoSystemRestart);
}

bool furi_system_policy_allow_phone_preview(void) {
    return policy_change(PolicyAllowPreview, 0);
}

bool furi_system_policy_is_recovery(void) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    bool result = policy.recovery;
    FURI_CRITICAL_EXIT();
    return result;
}
