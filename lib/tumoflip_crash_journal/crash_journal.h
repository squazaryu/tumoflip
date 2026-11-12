/** Bounded last-crash RTC record. Registers 0..7 (system/PIN) are untouched.
 * Legacy records use registers 8..14. OOM records use 8..19, marker written last.
 * Active-app context is volatile RAM: it need not survive reboot and must not
 * overwrite a retained OOM record. Labels are bounded/sanitized, not raw payloads.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <furi_hal_rtc.h>

#define TUMOFLIP_CRASH_JOURNAL_MAGIC         0x54434A31UL
#define TUMOFLIP_CRASH_OOM_MAGIC             0x54434A32UL
#define TUMOFLIP_CRASH_JOURNAL_APP_LENGTH    12U
#define TUMOFLIP_CRASH_JOURNAL_THREAD_LENGTH 8U
#define TUMOFLIP_CRASH_HEAP_MASK             0x3FFFFUL
#define TUMOFLIP_CRASH_UPTIME_MASK           0x0FFFFFFFUL

typedef enum {
    TumoflipCrashKindNone = 0,
    TumoflipCrashKindHardFault = 1,
    TumoflipCrashKindNullPointer = 2,
    TumoflipCrashKindMemManage = 3,
    TumoflipCrashKindBusFault = 4,
    TumoflipCrashKindUsageFault = 5,
    TumoflipCrashKindWatchdog = 6,
    TumoflipCrashKindOther = 7,
    TumoflipCrashKindOutOfMemory = 8,
} TumoflipCrashKind;

typedef struct {
    uint32_t requested;
    uint32_t free_heap;
    uint32_t largest_block;
    uint32_t uptime_s;
    char thread[TUMOFLIP_CRASH_JOURNAL_THREAD_LENGTH + 1U];
} TumoflipOOMContext;

typedef struct {
    TumoflipCrashKind kind;
    uint16_t sequence;
    uint32_t commit;
    char app_id[TUMOFLIP_CRASH_JOURNAL_APP_LENGTH + 1U];
    bool has_oom_context;
    TumoflipOOMContext oom;
} TumoflipCrashReport;

/* Private core API, not a firmware SDK export. */
void tumoflip_crash_journal_set_active_app(const char* app_id, const char* git_hash);
void tumoflip_crash_journal_clear_active_app(void);
bool tumoflip_crash_journal_read(TumoflipCrashReport* report);
void tumoflip_crash_journal_record(TumoflipCrashKind kind, const char* git_hash);
void tumoflip_crash_journal_record_oom(const char* git_hash, const TumoflipOOMContext* context);
void tumoflip_crash_journal_ack(void);
TumoflipCrashKind tumoflip_crash_journal_classify(const char* message);
const char* tumoflip_crash_journal_kind_name(TumoflipCrashKind kind);
