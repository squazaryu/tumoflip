#include <tumoflip_crash_journal/crash_journal.h>

/* Keep the fatal-path serializer shared, including across LTO call sites. */
#if defined(__GNUC__) && !defined(__clang__)
#define CRASH_NOINLINE __attribute__((noinline, noclone))
#else
#define CRASH_NOINLINE __attribute__((noinline))
#endif

static struct {
    uint32_t words[3];
    volatile uint32_t commit;
} tumoflip_crash_active;

static CRASH_NOINLINE uint32_t tumoflip_crash_journal_commit(const char* git_hash) {
    if(!git_hash) return 0;
    uint32_t result = 0;
    size_t digits = 0;
    while(digits < 8U && git_hash[digits]) {
        char c = git_hash[digits];
        uint32_t digit;
        if(c >= '0' && c <= '9')
            digit = (uint32_t)(c - '0');
        else if(c >= 'a' && c <= 'f')
            digit = (uint32_t)(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F')
            digit = (uint32_t)(c - 'A' + 10);
        else
            break;
        result = (result << 4U) | digit;
        digits++;
    }
    return digits >= 7U ? result : 0;
}

static inline bool tumoflip_crash_journal_app_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static CRASH_NOINLINE void
    tumoflip_crash_pack_name(uint32_t* words, const char* name, size_t length) {
    memset(words, 0, length);
    if(name) {
        for(size_t i = 0; i < length && name[i]; ++i) {
            char c = tumoflip_crash_journal_app_char(name[i]) ? name[i] : '_';
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            ((uint8_t*)words)[i] = (uint8_t)c;
#else
            words[i / 4U] |= (uint32_t)(uint8_t)c << ((i % 4U) * 8U);
#endif
        }
    }
}

static inline void tumoflip_crash_unpack_name(char* output, const uint32_t* words, size_t length) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(output, words, length);
#else
    for(size_t i = 0; i < length; ++i)
        output[i] = (char)(words[i / 4U] >> ((i % 4U) * 8U));
#endif
}

void tumoflip_crash_journal_set_active_app(const char* app_id, const char* git_hash) {
    tumoflip_crash_active.commit = 0;
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    tumoflip_crash_pack_name(
        tumoflip_crash_active.words, app_id, TUMOFLIP_CRASH_JOURNAL_APP_LENGTH);
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    tumoflip_crash_active.commit = tumoflip_crash_journal_commit(git_hash);
}

void tumoflip_crash_journal_clear_active_app(void) {
    tumoflip_crash_active.commit = 0;
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
    memset(tumoflip_crash_active.words, 0, sizeof(tumoflip_crash_active.words));
}

static inline uint32_t
    tumoflip_crash_journal_checksum(uint32_t meta, uint32_t commit, const uint32_t app[3]) {
    return TUMOFLIP_CRASH_JOURNAL_MAGIC ^ 0xA56C3D19UL ^ meta ^ commit ^ app[0] ^ app[1] ^ app[2];
}

static inline uint32_t tumoflip_crash_checksum(const uint32_t data[10], bool extended) {
    uint32_t checksum = tumoflip_crash_journal_checksum(data[0], data[1], &data[2]);
    if(extended) {
        checksum ^= TUMOFLIP_CRASH_JOURNAL_MAGIC ^ TUMOFLIP_CRASH_OOM_MAGIC;
        for(size_t i = 5; i < 10; ++i)
            checksum ^= data[i];
    }
    return checksum;
}

static CRASH_NOINLINE bool tumoflip_crash_journal_read_base(uint32_t data[10], bool* extended) {
    uint32_t marker = furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipCrashMarker);
    if(marker != TUMOFLIP_CRASH_JOURNAL_MAGIC && marker != TUMOFLIP_CRASH_OOM_MAGIC) return false;
    *extended = marker == TUMOFLIP_CRASH_OOM_MAGIC;
    for(size_t i = 0; i < 5; ++i)
        data[i] = furi_hal_rtc_get_register(
            (FuriHalRtcRegister)(FuriHalRtcRegisterTumoflipCrashMeta + i));
    if(*extended) {
        for(size_t i = 5; i < 10; ++i)
            data[i] = furi_hal_rtc_get_register(
                (FuriHalRtcRegister)(FuriHalRtcRegisterTumoflipOOMRequested + i - 5));
    }
    uint32_t kind = data[0] >> 24U;
    return kind > TumoflipCrashKindNone && kind <= TumoflipCrashKindOutOfMemory &&
           (!*extended || kind == TumoflipCrashKindOutOfMemory) && (uint16_t)data[0] != 0 &&
           furi_hal_rtc_get_register(FuriHalRtcRegisterTumoflipCrashChecksum) ==
               tumoflip_crash_checksum(data, *extended);
}

bool tumoflip_crash_journal_read(TumoflipCrashReport* report) {
    uint32_t data[10];
    bool extended;
    if(!report || !tumoflip_crash_journal_read_base(data, &extended)) return false;
    memset(report, 0, sizeof(*report));
    report->kind = (TumoflipCrashKind)(data[0] >> 24U);
    report->sequence = (uint16_t)data[0];
    report->commit = data[1];
    tumoflip_crash_unpack_name(report->app_id, &data[2], TUMOFLIP_CRASH_JOURNAL_APP_LENGTH);
    if(extended) {
        report->has_oom_context = true;
        report->oom.requested = data[5];
        uint32_t available = data[6] & TUMOFLIP_CRASH_HEAP_MASK;
        uint32_t largest = (data[6] >> 18U) | ((data[7] & 15U) << 14U);
        report->oom.free_heap = available == TUMOFLIP_CRASH_HEAP_MASK ? UINT32_MAX : available;
        report->oom.largest_block = largest == TUMOFLIP_CRASH_HEAP_MASK ? UINT32_MAX : largest;
        report->oom.uptime_s = data[7] >> 4U;
        tumoflip_crash_unpack_name(
            report->oom.thread, &data[8], TUMOFLIP_CRASH_JOURNAL_THREAD_LENGTH);
    }
    return true;
}

static CRASH_NOINLINE void tumoflip_crash_journal_record_full(
    TumoflipCrashKind kind,
    const char* git_hash,
    const TumoflipOOMContext* context) {
    if(kind == TumoflipCrashKindNone || kind > TumoflipCrashKindOutOfMemory) return;
    uint32_t data[10] = {0};
    bool previous_extended;
    uint16_t sequence =
        tumoflip_crash_journal_read_base(data, &previous_extended) ? (uint16_t)(data[0] + 1U) : 1U;
    memset(data, 0, sizeof(data));
    data[0] = ((uint32_t)kind << 24U) | (sequence ? sequence : 1U);
    data[1] = tumoflip_crash_journal_commit(git_hash);
    if(data[1] && tumoflip_crash_active.commit == data[1])
        memcpy(&data[2], tumoflip_crash_active.words, 3U * sizeof(uint32_t));
    bool extended = context && context->requested && kind == TumoflipCrashKindOutOfMemory;
    if(extended) {
        data[5] = context->requested;
        /* 18 bits covers the entire physical C1 heap; all-ones means unavailable. */
        uint32_t available = context->free_heap < TUMOFLIP_CRASH_HEAP_MASK ?
                                 context->free_heap :
                                 TUMOFLIP_CRASH_HEAP_MASK;
        uint32_t largest = context->largest_block < TUMOFLIP_CRASH_HEAP_MASK ?
                               context->largest_block :
                               TUMOFLIP_CRASH_HEAP_MASK;
        uint32_t uptime = context->uptime_s < TUMOFLIP_CRASH_UPTIME_MASK ?
                              context->uptime_s :
                              TUMOFLIP_CRASH_UPTIME_MASK;
        data[6] = available | (largest << 18U);
        data[7] = (largest >> 14U) | (uptime << 4U);
        tumoflip_crash_pack_name(&data[8], context->thread, TUMOFLIP_CRASH_JOURNAL_THREAD_LENGTH);
    }
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashMarker, 0);
    for(size_t i = 0; i < 5; ++i)
        furi_hal_rtc_set_register(
            (FuriHalRtcRegister)(FuriHalRtcRegisterTumoflipCrashMeta + i), data[i]);
    for(size_t i = 5; i < 10; ++i)
        furi_hal_rtc_set_register(
            (FuriHalRtcRegister)(FuriHalRtcRegisterTumoflipOOMRequested + i - 5), data[i]);
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterTumoflipCrashChecksum, tumoflip_crash_checksum(data, extended));
    furi_hal_rtc_set_register(
        FuriHalRtcRegisterTumoflipCrashMarker,
        extended ? TUMOFLIP_CRASH_OOM_MAGIC : TUMOFLIP_CRASH_JOURNAL_MAGIC);
}

void tumoflip_crash_journal_record(TumoflipCrashKind kind, const char* git_hash) {
    tumoflip_crash_journal_record_full(kind, git_hash, NULL);
}

void tumoflip_crash_journal_record_oom(const char* git_hash, const TumoflipOOMContext* context) {
    tumoflip_crash_journal_record_full(TumoflipCrashKindOutOfMemory, git_hash, context);
}

void tumoflip_crash_journal_ack(void) {
    furi_hal_rtc_set_register(FuriHalRtcRegisterTumoflipCrashMarker, 0);
}

TumoflipCrashKind tumoflip_crash_journal_classify(const char* message) {
    if(!message) return TumoflipCrashKindOther;
    if(strcmp(message, "out of memory") == 0) return TumoflipCrashKindOutOfMemory;
    if(strcmp(message, "HardFault") == 0) return TumoflipCrashKindHardFault;
    if(strcmp(message, "NULL pointer dereference") == 0) return TumoflipCrashKindNullPointer;
    if(strncmp(message, "MemManage", 9U) == 0 || strcmp(message, "StackOverflow") == 0 ||
       strncmp(message, "MPU fault", 9U) == 0)
        return TumoflipCrashKindMemManage;
    if(strcmp(message, "BusFault") == 0) return TumoflipCrashKindBusFault;
    if(strcmp(message, "UsageFault") == 0) return TumoflipCrashKindUsageFault;
    return TumoflipCrashKindOther;
}

const char* tumoflip_crash_journal_kind_name(TumoflipCrashKind kind) {
    switch(kind) {
    case TumoflipCrashKindHardFault:
        return "hardfault";
    case TumoflipCrashKindNullPointer:
        return "null_pointer";
    case TumoflipCrashKindMemManage:
        return "memory_fault";
    case TumoflipCrashKindBusFault:
        return "bus_fault";
    case TumoflipCrashKindUsageFault:
        return "usage_fault";
    case TumoflipCrashKindWatchdog:
        return "watchdog";
    case TumoflipCrashKindOther:
        return "other";
    case TumoflipCrashKindOutOfMemory:
        return "oom";
    default:
        return "none";
    }
}
