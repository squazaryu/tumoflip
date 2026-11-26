#pragma once

/* Private core coordination, deliberately not included in furi.h or the SDK. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    TumoSystemWrite,
    TumoSystemPreview,
    TumoSystemPhonePreview,
} TumoSystemActivity;

typedef enum {
    TumoSystemNotClosing,
    TumoSystemPhoneDisconnect,
    TumoSystemRestart,
} TumoSystemBarrier;

typedef struct {
    uint16_t writes, previews, phone_previews;
    uint8_t closing, boot_crashes;
    bool display_on, foreground, transfer, phone_allowed, recovery;
    uint32_t transfer_until, closing_until;
} TumoSystemState;

void tumo_system_model_init(TumoSystemState* state);
bool tumo_system_model_acquire(TumoSystemState* state, TumoSystemActivity activity);
void tumo_system_model_release(TumoSystemState* state, TumoSystemActivity activity);
bool tumo_system_model_transfer(TumoSystemState* state, bool active, uint32_t now, uint32_t lease);
void tumo_system_model_expire(TumoSystemState* state, uint32_t now);
bool tumo_system_model_busy(TumoSystemState* state, uint32_t now);
bool tumo_system_model_quiet(const TumoSystemState* state);
bool tumo_system_model_quiesce(
    TumoSystemState* state,
    TumoSystemBarrier reason,
    uint32_t now,
    uint32_t timeout);
/* The journal owns the atomic, redundant streak byte in reserved metadata bits. */
uint8_t tumoflip_crash_journal_boot_streak(void);
void tumoflip_crash_journal_clear_boot_streak(void);

TumoSystemState furi_system_policy_snapshot(void);
bool furi_system_policy_acquire(TumoSystemActivity activity);
void furi_system_policy_release(TumoSystemActivity activity);
bool furi_system_policy_transfer(bool active);
void furi_system_policy_application(bool active);
void furi_system_policy_display(bool on);
void furi_system_policy_listen(void (*callback)(void*), void* context);
bool furi_system_policy_pause_phone_preview(void);
bool furi_system_policy_allow_phone_preview(void);
bool furi_system_policy_prepare_normal_restart(void);
bool furi_system_policy_is_recovery(void);
