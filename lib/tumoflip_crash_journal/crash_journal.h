/** Bounded, redacted last-crash record in STM32WB RTC backup registers.
 *
 * The marker is written last, so a reset during a record update cannot make
 * partial data look valid. No RF/NFC data, app arguments, or crash strings are
 * persisted. The application ID is limited to twelve safe ASCII characters.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <furi_hal_rtc.h>

#define TUMOFLIP_CRASH_JOURNAL_MAGIC 0x54434A31UL
#define TUMOFLIP_CRASH_JOURNAL_APP_LENGTH 12U
#define TUMOFLIP_CRASH_OOM_MAGIC 0x54434A32UL

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
    uint32_t tick_ms;
    char thread[TUMOFLIP_CRASH_JOURNAL_APP_LENGTH + 1U];
} TumoflipOOMContext;

typedef struct {
    TumoflipCrashKind kind;
    uint16_t sequence;
    uint32_t commit;
    char app_id[TUMOFLIP_CRASH_JOURNAL_APP_LENGTH + 1U];
    bool has_oom_context;
    TumoflipOOMContext oom;
} TumoflipCrashReport;

static inline uint32_t tumoflip_crash_journal_commit(const char* git_hash) {
    if(!git_hash) return 0;
    uint32_t result = 0;
    size_t digits = 0;
    while(digits < 8U && git_hash[digits]) {
        char c = git_hash[digits];
        uint32_t digit;
        if(c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if(c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A' + 10);
        else break;
        result = (result << 4U) | digit;
        digits++;
    }
    return digits >= 7U ? result : 0;
}

static inline bool tumoflip_crash_journal_app_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static inline void tumoflip_crash_journal_set_active_app(
    const char* app_id,
    const char* git_hash) {
    uint32_t words[3] = {0};
    if(app_id) {
        for(size_t i = 0; i < TUMOFLIP_CRASH_JOURNAL_APP_LENGTH && app_id[i]; i++) {
            const char c = tumoflip_crash_journal_app_char(app_id[i]) ? app_id[i] : '_';
            words[i / 4U] |= (uint32_t)(uint8_t)c << ((i % 4U) * 8U);
        }
    }
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveCommit, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveApp0, words[0]);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveApp1, words[1]);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveApp2, words[2]);
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterTumoflipActiveCommit, tumoflip_crash_journal_commit(git_hash));
}

static inline void tumoflip_crash_journal_clear_active_app(void) {
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveCommit, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveApp0, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveApp1, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipActiveApp2, 0);
}

static inline uint32_t tumoflip_crash_journal_checksum(
    uint32_t meta,
    uint32_t commit,
    const uint32_t app[3]) {
    return TUMOFLIP_CRASH_JOURNAL_MAGIC ^ 0xA56C3D19UL ^ meta ^ commit ^
           app[0] ^ app[1] ^ app[2];
}

static inline bool tumoflip_crash_journal_read_base(uint32_t data[5]) {
    if(furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipCrashMarker) !=
       TUMOFLIP_CRASH_JOURNAL_MAGIC) {
        return false;
    }
    for(size_t i = 0; i < 5U; ++i)
        data[i] = furi_hal_rtc_get_register((FuriHalRtcRegister)(FuriHalRtcRegisterTumoflipCrashMeta + i));
    if(furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipCrashChecksum) !=
       tumoflip_crash_journal_checksum(data[0], data[1], &data[2])) {
        return false;
    }
    const uint32_t kind = data[0] >> 24U;
    return kind > TumoflipCrashKindNone && kind <= TumoflipCrashKindOutOfMemory &&
           (uint16_t)data[0] != 0;
}

static inline uint32_t tumoflip_crash_oom_checksum(const uint32_t words[7]) {
    uint32_t checksum = TUMOFLIP_CRASH_OOM_MAGIC ^
                        furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipCrashChecksum);
    for(size_t i = 0; i < 7U; ++i) checksum ^= words[i];
    return checksum;
}

static inline bool tumoflip_crash_journal_read(TumoflipCrashReport* report) {
    uint32_t data[5];
    if(!report || !tumoflip_crash_journal_read_base(data)) return false;
    memset(report, 0, sizeof(*report));
    report->kind = (TumoflipCrashKind)(data[0] >> 24U);
    report->sequence = (uint16_t)data[0];
    report->commit = data[1];
    for(size_t i = 0; i < TUMOFLIP_CRASH_JOURNAL_APP_LENGTH; i++) {
        report->app_id[i] = (char)((data[2U + i / 4U] >> ((i % 4U) * 8U)) & 0xFFU);
    }
    if(report->kind == TumoflipCrashKindOutOfMemory &&
       furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipOOMMarker) == TUMOFLIP_CRASH_OOM_MAGIC) {
        uint32_t words[7];
        for(size_t i = 0; i < 7U; ++i)
            words[i] = furi_hal_rtc_get_register((FuriHalRtcRegister)(FuriHalRtcRegisterTumoflipOOMRequested + i));
        if(furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipOOMChecksum) ==
           tumoflip_crash_oom_checksum(words)) {
            report->has_oom_context = true;
            report->oom.requested = words[0];
            report->oom.free_heap = words[1];
            report->oom.largest_block = words[2];
            report->oom.tick_ms = words[3];
            for(size_t i = 0; i < TUMOFLIP_CRASH_JOURNAL_APP_LENGTH; ++i)
                report->oom.thread[i] = (char)(words[4U + i / 4U] >> ((i % 4U) * 8U));
        }
    }
    return true;
}

static inline void tumoflip_crash_journal_record(
    TumoflipCrashKind kind,
    const char* git_hash) {
    if(kind == TumoflipCrashKindNone || kind > TumoflipCrashKindOutOfMemory) return;
    uint32_t previous[5];
    const uint16_t sequence = tumoflip_crash_journal_read_base(previous) ?
                                  (uint16_t)(previous[0] + 1U) : 1U;
    const uint16_t next_sequence = sequence ? sequence : 1U;
    const uint32_t commit = tumoflip_crash_journal_commit(git_hash);
    const uint32_t active_commit =
        furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipActiveCommit);
    const bool app_is_current = commit && active_commit == commit;
    const uint32_t app[3] = {
        app_is_current ? furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipActiveApp0) : 0,
        app_is_current ? furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipActiveApp1) : 0,
        app_is_current ? furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipActiveApp2) : 0,
    };
    const uint32_t meta = ((uint32_t)kind << 24U) | next_sequence;

    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashMarker, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipOOMMarker, 0);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashMeta, meta);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashCommit, commit);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashApp0, app[0]);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashApp1, app[1]);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashApp2, app[2]);
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterTumoflipCrashChecksum,
        tumoflip_crash_journal_checksum(meta, commit, app));
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterTumoflipCrashMarker, TUMOFLIP_CRASH_JOURNAL_MAGIC);
}

static inline void tumoflip_crash_journal_attach_oom(const TumoflipOOMContext* context) {
    uint32_t base[5];
    if(!context || !context->requested || !tumoflip_crash_journal_read_base(base) ||
       base[0] >> 24U != TumoflipCrashKindOutOfMemory) return;
    uint32_t words[7] = {context->requested, context->free_heap, context->largest_block, context->tick_ms};
    for(size_t i = 0; i < TUMOFLIP_CRASH_JOURNAL_APP_LENGTH && context->thread[i]; ++i) {
        const char c = tumoflip_crash_journal_app_char(context->thread[i]) ? context->thread[i] : '_';
        words[4U + i / 4U] |= (uint32_t)(uint8_t)c << ((i % 4U) * 8U);
    }
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipOOMMarker, 0);
    for(size_t i = 0; i < 7U; ++i)
        furi_hal_rtc_set_register((FuriHalRtcRegister)(FuriHalRtcRegisterTumoflipOOMRequested + i), words[i]);
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipOOMChecksum, tumoflip_crash_oom_checksum(words));
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipOOMMarker, TUMOFLIP_CRASH_OOM_MAGIC);
}

static inline void tumoflip_crash_journal_ack(void) {
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashMarker, 0);
}

static inline TumoflipCrashKind tumoflip_crash_journal_classify(const char* message) {
    if(!message) return TumoflipCrashKindOther;
    if(strcmp(message, "out of memory") == 0) return TumoflipCrashKindOutOfMemory;
    if(strcmp(message, "HardFault") == 0) return TumoflipCrashKindHardFault;
    if(strcmp(message, "NULL pointer dereference") == 0) return TumoflipCrashKindNullPointer;
    if(strncmp(message, "MemManage", 9U) == 0 || strcmp(message, "StackOverflow") == 0 ||
       strncmp(message, "MPU fault", 9U) == 0) {
        return TumoflipCrashKindMemManage;
    }
    if(strcmp(message, "BusFault") == 0) return TumoflipCrashKindBusFault;
    if(strcmp(message, "UsageFault") == 0) return TumoflipCrashKindUsageFault;
    return TumoflipCrashKindOther;
}

static inline const char* tumoflip_crash_journal_kind_name(TumoflipCrashKind kind) {
    switch(kind) {
    case TumoflipCrashKindHardFault: return "hardfault";
    case TumoflipCrashKindNullPointer: return "null_pointer";
    case TumoflipCrashKindMemManage: return "memory_fault";
    case TumoflipCrashKindBusFault: return "bus_fault";
    case TumoflipCrashKindUsageFault: return "usage_fault";
    case TumoflipCrashKindWatchdog: return "watchdog";
    case TumoflipCrashKindOther: return "other";
    case TumoflipCrashKindOutOfMemory: return "oom";
    default: return "none";
    }
}
