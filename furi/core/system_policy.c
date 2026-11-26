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

bool furi_system_policy_acquire(TumoSystemActivity activity) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    tumo_system_model_expire(&policy, furi_get_tick());
    bool result = tumo_system_model_acquire(&policy, activity);
    FURI_CRITICAL_EXIT();
    notify(before);
    return result;
}

void furi_system_policy_release(TumoSystemActivity activity) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    tumo_system_model_release(&policy, activity);
    FURI_CRITICAL_EXIT();
    notify(before);
}

bool furi_system_policy_transfer(bool active) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    bool result =
        tumo_system_model_transfer(&policy, active, furi_get_tick(), furi_ms_to_ticks(30000));
    FURI_CRITICAL_EXIT();
    notify(before);
    return result;
}

void furi_system_policy_application(bool active) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    policy.foreground = active;
    FURI_CRITICAL_EXIT();
    notify(before);
}

void furi_system_policy_display(bool on) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    policy.display_on = on;
    FURI_CRITICAL_EXIT();
    notify(before);
}

void furi_system_policy_listen(void (*callback)(void*), void* context) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    listener = callback;
    listener_context = context;
    FURI_CRITICAL_EXIT();
}

static bool quiesce(TumoSystemBarrier reason) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    bool result =
        tumo_system_model_quiesce(&policy, reason, furi_get_tick(), furi_ms_to_ticks(5000));
    if(result && reason == TumoSystemRestart) tumoflip_crash_journal_clear_boot_streak();
    FURI_CRITICAL_EXIT();
    notify(before);
    return result;
}

bool furi_system_policy_pause_phone_preview(void) {
    return quiesce(TumoSystemPhoneDisconnect);
}

bool furi_system_policy_prepare_normal_restart(void) {
    return quiesce(TumoSystemRestart);
}

bool furi_system_policy_allow_phone_preview(void) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    uint32_t before = signature();
    tumo_system_model_expire(&policy, furi_get_tick());
    bool result = policy.closing == TumoSystemNotClosing;
    if(result) policy.phone_allowed = true;
    FURI_CRITICAL_EXIT();
    notify(before);
    return result;
}

bool furi_system_policy_is_recovery(void) {
    FURI_CRITICAL_ENTER();
    ensure_initialized();
    bool result = policy.recovery;
    FURI_CRITICAL_EXIT();
    return result;
}
