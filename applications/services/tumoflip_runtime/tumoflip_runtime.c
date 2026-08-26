#include <bt/bt_service/bt_i.h>
#include <cli/cli.h>
#include <furi.h>
#include <furi_hal_info.h>
#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <furi_hal_version.h>
#include <tumoflip_runtime/tumoflip_runtime.h>
#include "tumofabric_core.h"
#include <storage/storage.h>
#include <subghz_radio_broker/subghz_radio_broker_i.h>
#include <stdio.h>
#include <string.h>
#include <stm32wbxx.h>
#include <toolbox/args.h>
#include <toolbox/cli/cli_command.h>
#include <toolbox/saved_struct.h>

#define TUMOFLIP_RUNTIME_APP_ID             "runtime"
#define TUMOFLIP_RUNTIME_QUEUE_DEPTH        8U
#define TUMOFLIP_RUNTIME_STATUS_MAX         160U
#define TUMOFLIP_RUNTIME_TRACE_DEPTH        8U
#define TUMOFLIP_RUNTIME_TRACE_MAX          160U
#define TUMOFLIP_RUNTIME_TWIN_MAX           160U
#define TUMOFLIP_RUNTIME_FABRIC_MAX         160U
#define TUMOFLIP_RUNTIME_DIAGNOSTIC_MAX     160U
#define TUMOFLIP_RUNTIME_RADIO_MAX          192U
#define TUMOFLIP_RUNTIME_RADIO_PROTOCOL_MAX 192U
#define TUMOFLIP_RUNTIME_JOURNAL_MAX        192U
#define TUMOFLIP_RUNTIME_JOURNAL_PATH       EXT_PATH(".tumoflip/runtime-journal.bin")
#define TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH   EXT_PATH(".tumoflip/runtime-journal.bin.tmp")
#define TUMOFLIP_RUNTIME_JOURNAL_BAK_PATH   EXT_PATH(".tumoflip/runtime-journal.bin.bak")
#define TUMOFLIP_RUNTIME_JOURNAL_MAGIC      0x54U
#define TUMOFLIP_RUNTIME_JOURNAL_VERSION    1U
#define TUMOFLIP_RUNTIME_DIAGNOSTIC_PATH    EXT_PATH(".tumoflip-runtime-diag.tmp")
#define TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_PATH EXT_PATH(".tumoflip/diagnostics-last.txt")
#define TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_TMP_PATH EXT_PATH(".tumoflip/diagnostics-last.txt.tmp")
#define TUMOFLIP_RUNTIME_RADIO_SESSIONS_PATH EXT_PATH(".tumoflip/radio-sessions.csv")
#define TUMOFLIP_RUNTIME_RADIO_SESSIONS_TMP_PATH EXT_PATH(".tumoflip/radio-sessions.csv.tmp")
#define TUMOFLIP_RUNTIME_RADIO_SESSIONS_BAK_PATH EXT_PATH(".tumoflip/radio-sessions.csv.bak")
#define TUMOFLIP_RUNTIME_SESSION_OWNER_MAX  24U
#define TUMOFLIP_RUNTIME_JOURNAL_APP_MAX    15U
#define TUMOFLIP_RUNTIME_JOURNAL_SCENE_MAX  23U
#define TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH EXT_PATH(".tumoflip/package-state.txt")
#define TUMOFLIP_RUNTIME_CAPABILITIES                                       \
    "runtime=1;fab=2;session=3;status=2;trace=1;twin=1;pkg=1;radio=2;sd=1;" \
    "fabric=1;diag=1;rc=1;rs=2;rp=1;" \
    "feat=pkg,radio,trace,twin,transfer,fabric;time=1;gps=1;net=1"

typedef struct {
    BtAppBridgeEvent event;
} TumoflipRuntimeMessage;

typedef struct {
    uint32_t session_id;
    char owner[TUMOFLIP_RUNTIME_SESSION_OWNER_MAX + 1];
} TumoflipRuntimeSession;

typedef struct {
    char code;
    char command;
    bool error;
} TumoflipRuntimeTraceEvent;

typedef enum {
    TumoflipRuntimeJournalNone,
    TumoflipRuntimeJournalCrash,
    TumoflipRuntimeJournalWatchdog,
    TumoflipRuntimeJournalBrownout,
    TumoflipRuntimeJournalReset,
} TumoflipRuntimeJournalKind;

typedef struct {
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t reset_flags;
    uint32_t fault_data;
    uint8_t kind;
    uint8_t reserved[3];
    char firmware_commit[9];
    char app_id[TUMOFLIP_RUNTIME_JOURNAL_APP_MAX + 1U];
    char scene[TUMOFLIP_RUNTIME_JOURNAL_SCENE_MAX + 1U];
} TumoflipRuntimeJournal;

typedef struct {
    Bt* bt;
    Storage* storage;
    SubGhzRadioBroker* radio_broker;
    FuriMessageQueue* queue;
    FuriMutex* trace_mutex;
    FuriMutex* fabric_mutex;
    FuriPubSubSubscription* subscription;
    TumoflipRuntimeApi api;
    TumoflipRuntimeSession session;
    TumoFabricState fabric;
    TumoflipRuntimeTraceEvent trace[TUMOFLIP_RUNTIME_TRACE_DEPTH];
    uint8_t trace_head;
    uint8_t trace_count;
    uint32_t trace_dropped;
    bool transfer_active;
    TumoflipRuntimeJournal journal;
} TumoflipRuntime;

static void tumoflip_runtime_trace_add(
    TumoflipRuntime* runtime,
    char code,
    const char* command,
    bool error);
static bool
    tumoflip_runtime_api_get_trace(TumoflipRuntimeApi* api, char* output, size_t output_size);
static bool tumoflip_runtime_api_get_fabric_state(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeFabricSnapshot* snapshot);
static bool tumoflip_runtime_api_cancel_fabric(TumoflipRuntimeApi* api);
static bool tumoflip_runtime_api_open_local_fabric(TumoflipRuntimeApi* api);
static bool tumoflip_runtime_api_step_local_fabric(TumoflipRuntimeApi* api, int8_t delta);
static bool tumoflip_runtime_api_get_diagnostics(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeDiagnosticReport* report);
static bool tumoflip_runtime_diagnostics_export(
    TumoflipRuntime* runtime,
    const TumoflipRuntimeDiagnosticReport* report);
static const char* tumoflip_runtime_str_or_unknown(const char* value);

static const char* tumoflip_runtime_diagnostic_check_name(
    TumoflipRuntimeDiagnosticCheckId id) {
    static const char* const names[TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT] = {
        "identity",
        "heap",
        "storage",
        "battery",
        "radio",
        "ble",
        "display",
        "nfc",
        "gpio",
        "input",
    };

    return id < TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT ? names[id] : "unknown";
}

static const char* tumoflip_runtime_diagnostic_status_name(
    TumoflipRuntimeDiagnosticStatus status) {
    switch(status) {
    case TumoflipRuntimeDiagnosticStatusPassed:
        return "pass";
    case TumoflipRuntimeDiagnosticStatusFailed:
        return "fail";
    case TumoflipRuntimeDiagnosticStatusSkipped:
    default:
        return "skip";
    }
}

static const char* tumoflip_runtime_radio_session_state_name(
    SubGhzRadioBrokerSessionState state) {
    switch(state) {
    case SubGhzRadioBrokerSessionStateActive:
        return "active";
    case SubGhzRadioBrokerSessionStatePaused:
        return "paused";
    case SubGhzRadioBrokerSessionStateClosed:
        return "closed";
    default:
        return "unknown";
    }
}

static void tumoflip_runtime_diagnostic_set(
    TumoflipRuntimeDiagnosticReport* report,
    TumoflipRuntimeDiagnosticCheckId id,
    TumoflipRuntimeDiagnosticStatus status,
    const char* detail) {
    TumoflipRuntimeDiagnosticCheck* check = &report->checks[id];
    check->id = id;
    check->status = status;
    snprintf(check->detail, sizeof(check->detail), "%s", detail ? detail : "none");
}

static bool tumoflip_runtime_storage_self_test(Storage* storage) {
    static const uint8_t pattern[] = {
        0x54,
        0x55,
        0x4D,
        0x4F,
        0x2D,
        0x44,
        0x49,
        0x41,
        0x47,
        0x01,
        0xA5,
        0x5A,
        0x00,
        0xFF,
        0x13,
        0x37,
    };
    uint8_t readback[sizeof(pattern)] = {0};
    bool success = false;
    File* file = storage_file_alloc(storage);

    if(storage_file_open(
           file,
           TUMOFLIP_RUNTIME_DIAGNOSTIC_PATH,
           FSAM_WRITE,
           FSOM_CREATE_ALWAYS)) {
        const bool written = storage_file_write(file, pattern, sizeof(pattern)) == sizeof(pattern);
        const bool synced = written && storage_file_sync(file);
        storage_file_close(file);

        if(synced &&
           storage_file_open(
               file,
               TUMOFLIP_RUNTIME_DIAGNOSTIC_PATH,
               FSAM_READ,
               FSOM_OPEN_EXISTING)) {
            success = storage_file_read(file, readback, sizeof(readback)) == sizeof(readback) &&
                      memcmp(pattern, readback, sizeof(pattern)) == 0;
            storage_file_close(file);
        } else {
            storage_file_close(file);
        }
    } else {
        storage_file_close(file);
    }

    storage_simply_remove(storage, TUMOFLIP_RUNTIME_DIAGNOSTIC_PATH);
    storage_file_free(file);
    return success;
}

static const char* tumoflip_runtime_journal_kind_name(uint8_t kind) {
    switch((TumoflipRuntimeJournalKind)kind) {
    case TumoflipRuntimeJournalCrash:
        return "crash";
    case TumoflipRuntimeJournalWatchdog:
        return "watchdog";
    case TumoflipRuntimeJournalBrownout:
        return "brownout";
    case TumoflipRuntimeJournalReset:
        return "reset";
    case TumoflipRuntimeJournalNone:
    default:
        return "none";
    }
}

static bool tumoflip_runtime_journal_save(
    TumoflipRuntime* runtime,
    const TumoflipRuntimeJournal* journal) {
    furi_check(runtime);
    furi_check(journal);

    storage_simply_mkdir(runtime->storage, EXT_PATH(".tumoflip"));
    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH);
    if(!saved_struct_save(
           TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH,
           journal,
           sizeof(*journal),
           TUMOFLIP_RUNTIME_JOURNAL_MAGIC,
           TUMOFLIP_RUNTIME_JOURNAL_VERSION)) {
        storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH);
        return false;
    }

    const bool destination_exists =
        storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_PATH);
    bool backup_moved = false;
    bool temp_moved = false;
    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_BAK_PATH);
    if(destination_exists) {
        if(storage_common_rename(
               runtime->storage,
               TUMOFLIP_RUNTIME_JOURNAL_PATH,
               TUMOFLIP_RUNTIME_JOURNAL_BAK_PATH) != FSE_OK) {
            storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH);
            return false;
        }
        backup_moved = true;
    }

    if(storage_common_rename(
           runtime->storage,
           TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH,
           TUMOFLIP_RUNTIME_JOURNAL_PATH) == FSE_OK) {
        temp_moved = true;
    }

    if(!temp_moved) {
        storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_PATH);
        if(backup_moved) {
            storage_common_rename(
                runtime->storage,
                TUMOFLIP_RUNTIME_JOURNAL_BAK_PATH,
                TUMOFLIP_RUNTIME_JOURNAL_PATH);
        }
        storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH);
        return false;
    }

    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_BAK_PATH);
    return true;
}

static bool tumoflip_runtime_journal_clear(TumoflipRuntime* runtime) {
    furi_check(runtime);
    const bool removed = storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_PATH);
    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_TMP_PATH);
    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_BAK_PATH);
    memset(&runtime->journal, 0, sizeof(runtime->journal));
    return removed || !storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_PATH);
}

static void tumoflip_runtime_journal_init(TumoflipRuntime* runtime) {
    memset(&runtime->journal, 0, sizeof(runtime->journal));
    if(storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_JOURNAL_PATH)) {
        saved_struct_load(
            TUMOFLIP_RUNTIME_JOURNAL_PATH,
            &runtime->journal,
            sizeof(runtime->journal),
            TUMOFLIP_RUNTIME_JOURNAL_MAGIC,
            TUMOFLIP_RUNTIME_JOURNAL_VERSION);
    }

    uint32_t reset_flags = 0U;
#ifdef RCC_CSR_PINRSTF
    reset_flags = RCC->CSR & (RCC_CSR_PINRSTF | RCC_CSR_BORRSTF | RCC_CSR_SFTRSTF |
                              RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF);
#endif
    const uint32_t fault_data = furi_hal_rtc_get_fault_data();
    TumoflipRuntimeJournalKind kind = TumoflipRuntimeJournalNone;
    if(fault_data != 0U) {
        kind = TumoflipRuntimeJournalCrash;
    }
#ifdef RCC_CSR_IWDGRSTF
    else if(reset_flags & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) {
        kind = TumoflipRuntimeJournalWatchdog;
    }
#endif
#ifdef RCC_CSR_BORRSTF
    else if(reset_flags & (RCC_CSR_BORRSTF | RCC_CSR_LPWRRSTF)) {
        kind = TumoflipRuntimeJournalBrownout;
    }
#endif
#ifdef RCC_CSR_PINRSTF
    else if(reset_flags & (RCC_CSR_PINRSTF | RCC_CSR_SFTRSTF)) {
        kind = TumoflipRuntimeJournalReset;
    }
#endif

    const bool is_new_fault = kind != TumoflipRuntimeJournalNone &&
                              (runtime->journal.kind != (uint8_t)kind ||
                               runtime->journal.reset_flags != reset_flags ||
                               runtime->journal.fault_data != fault_data);
    if(is_new_fault) {
        runtime->journal.sequence++;
        if(runtime->journal.sequence == 0U) runtime->journal.sequence = 1U;
        runtime->journal.timestamp = furi_hal_rtc_get_timestamp();
        runtime->journal.reset_flags = reset_flags;
        runtime->journal.fault_data = fault_data;
        runtime->journal.kind = (uint8_t)kind;
        strlcpy(runtime->journal.app_id, TUMOFLIP_RUNTIME_APP_ID, sizeof(runtime->journal.app_id));
        strlcpy(runtime->journal.scene, "boot", sizeof(runtime->journal.scene));
        const Version* version = furi_hal_version_get_firmware_version();
        strlcpy(
            runtime->journal.firmware_commit,
            tumoflip_runtime_str_or_unknown(version_get_githash(version)),
            sizeof(runtime->journal.firmware_commit));
        if(!tumoflip_runtime_journal_save(runtime, &runtime->journal)) {
            FURI_LOG_W("TumoRuntime", "Unable to persist reset journal");
        }
    }

#ifdef RCC_CSR_RMVF
    RCC->CSR |= RCC_CSR_RMVF;
#endif
}

bool tumoflip_runtime_diagnostics_run(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeDiagnosticReport* report) {
    furi_assert(api);
    furi_assert(report);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    memset(report, 0, sizeof(*report));
    report->schema_version = 1U;
    report->check_count = TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT;
    for(uint8_t id = 0; id < TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT; id++) {
        report->checks[id].id = (TumoflipRuntimeDiagnosticCheckId)id;
        report->checks[id].status = TumoflipRuntimeDiagnosticStatusSkipped;
        snprintf(
            report->checks[id].detail,
            sizeof(report->checks[id].detail),
            "not-run");
    }

    const Version* version = furi_hal_version_get_firmware_version();
    uint16_t api_major = 0;
    uint16_t api_minor = 0;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    char identity[sizeof(report->checks[0].detail)];
    snprintf(
        identity,
        sizeof(identity),
        "fw=%.8s,api=%u.%u,target=%u",
        version_get_version(version) ? version_get_version(version) : "unknown",
        api_major,
        api_minor,
        version_get_target(version));
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckIdentity,
        TumoflipRuntimeDiagnosticStatusPassed,
        identity);

    char heap[sizeof(report->checks[0].detail)];
    snprintf(
        heap,
        sizeof(heap),
        "free=%lu,max=%lu,min=%lu",
        (unsigned long)memmgr_get_free_heap(),
        (unsigned long)memmgr_heap_get_max_free_block(),
        (unsigned long)memmgr_get_minimum_free_heap());
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckHeap,
        TumoflipRuntimeDiagnosticStatusPassed,
        heap);

    if(storage_sd_status(runtime->storage) == FSE_OK) {
        tumoflip_runtime_diagnostic_set(
            report,
            TumoflipRuntimeDiagnosticCheckStorage,
            tumoflip_runtime_storage_self_test(runtime->storage) ?
                TumoflipRuntimeDiagnosticStatusPassed : TumoflipRuntimeDiagnosticStatusFailed,
            "sd-read-write");
    } else {
        tumoflip_runtime_diagnostic_set(
            report,
            TumoflipRuntimeDiagnosticCheckStorage,
            TumoflipRuntimeDiagnosticStatusSkipped,
            "sd-not-ready");
    }

    char battery[sizeof(report->checks[0].detail)];
    snprintf(
        battery,
        sizeof(battery),
        "pct=%u,charging=%u",
        furi_hal_power_get_pct(),
        furi_hal_power_is_charging() ? 1U : 0U);
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckBattery,
        TumoflipRuntimeDiagnosticStatusPassed,
        battery);

    SubGhzRadioBrokerStatusV2 radio_status;
    subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status);
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckRadio,
        (!radio_status.base.busy && radio_status.state == SubGhzRadioBrokerStateIdle) ?
            TumoflipRuntimeDiagnosticStatusPassed :
            TumoflipRuntimeDiagnosticStatusSkipped,
        (!radio_status.base.busy && radio_status.state == SubGhzRadioBrokerStateIdle) ?
            "idle" :
            "busy-no-interrupt");

    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckBle,
        bt_app_bridge_get_pubsub(runtime->bt) ? TumoflipRuntimeDiagnosticStatusPassed :
                                                TumoflipRuntimeDiagnosticStatusFailed,
        bt_app_bridge_get_pubsub(runtime->bt) ? "bridge-ready" : "bridge-missing");
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckDisplay,
        TumoflipRuntimeDiagnosticStatusSkipped,
        "interactive-check-required");
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckNfc,
        TumoflipRuntimeDiagnosticStatusSkipped,
        "radio-check-requires-card");
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckGpio,
        TumoflipRuntimeDiagnosticStatusSkipped,
        "external-test-required");
    tumoflip_runtime_diagnostic_set(
        report,
        TumoflipRuntimeDiagnosticCheckInput,
        TumoflipRuntimeDiagnosticStatusSkipped,
        "interactive-check-required");
    return true;
}

bool tumoflip_runtime_diagnostics_format(
    const TumoflipRuntimeDiagnosticReport* report,
    char* output,
    size_t output_size) {
    if(!report || !output || output_size == 0U ||
       report->check_count > TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT) {
        return false;
    }

    size_t used = 0;
    int written = snprintf(output, output_size, "schema=%u", report->schema_version);
    if(written < 0 || (size_t)written >= output_size) return false;
    used = (size_t)written;

    for(uint8_t index = 0; index < report->check_count; index++) {
        const TumoflipRuntimeDiagnosticCheck* check = &report->checks[index];
        written = snprintf(
            &output[used],
            output_size - used,
            ";%s=%s",
            tumoflip_runtime_diagnostic_check_name(check->id),
            tumoflip_runtime_diagnostic_status_name(check->status));
        if(written < 0 || (size_t)written >= output_size - used) return false;
        used += (size_t)written;
    }

    return true;
}

static void tumoflip_runtime_cli_print_error(const char* error) {
    printf("FABRIC schema=1;status=error;error=%s\r\n", error);
}

static void tumoflip_runtime_cli_print_snapshot(TumoflipRuntime* runtime) {
    TumoflipRuntimeFabricSnapshot snapshot;
    if(!tumoflip_runtime_api_get_fabric_state(&runtime->api, &snapshot)) {
        tumoflip_runtime_cli_print_error("runtime");
        return;
    }

    printf(
        "FABRIC schema=1;status=ok;active=%u;owner=%s;seq=%lu;value=%d;persist=ram\r\n",
        snapshot.active ? 1U : 0U,
        snapshot.active ? snapshot.owner : "none",
        (unsigned long)snapshot.last_sequence,
        snapshot.counter);
}

static void tumoflip_runtime_cli_print_diagnostics(TumoflipRuntime* runtime) {
    TumoflipRuntimeDiagnosticReport report;
    if(!tumoflip_runtime_api_get_diagnostics(&runtime->api, &report)) {
        printf("DIAG schema=1;status=error;error=runtime\r\n");
        return;
    }

    printf("DIAG schema=%u;count=%u\r\n", report.schema_version, report.check_count);
    for(uint8_t index = 0; index < report.check_count; index++) {
        const TumoflipRuntimeDiagnosticCheck* check = &report.checks[index];
        printf(
            "DIAG check=%s;status=%s;detail=%s\r\n",
            tumoflip_runtime_diagnostic_check_name(check->id),
            tumoflip_runtime_diagnostic_status_name(check->status),
            check->detail);
    }
    printf(
        "DIAG export=%s;path=%s\r\n",
        tumoflip_runtime_diagnostics_export(runtime, &report) ? "ok" : "failed",
        TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_PATH);
}

static bool tumoflip_runtime_diagnostics_export(
    TumoflipRuntime* runtime,
    const TumoflipRuntimeDiagnosticReport* report) {
    if(!runtime || !report) return false;
    if(storage_simply_mkdir(runtime->storage, EXT_PATH(".tumoflip")) == false) return false;

    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_TMP_PATH);
    File* file = storage_file_alloc(runtime->storage);
    bool success = storage_file_open(
        file, TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        char line[TUMOFLIP_RUNTIME_DIAGNOSTIC_MAX];
        success = storage_file_write(file, "Tumo Runtime diagnostics\n", 25U) == 25U;
        for(uint8_t index = 0U; success && index < report->check_count; index++) {
            const TumoflipRuntimeDiagnosticCheck* check = &report->checks[index];
            const int written = snprintf(
                line,
                sizeof(line),
                "%s=%s (%s)\n",
                tumoflip_runtime_diagnostic_check_name(check->id),
                tumoflip_runtime_diagnostic_status_name(check->status),
                check->detail);
            success = written > 0 && (size_t)written < sizeof(line) &&
                      storage_file_write(file, line, (size_t)written) == (size_t)written;
        }
        success = success && storage_file_sync(file);
        storage_file_close(file);
    } else {
        storage_file_close(file);
    }
    storage_file_free(file);

    if(success) {
        storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_PATH);
        success = storage_common_rename(
                      runtime->storage,
                      TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_TMP_PATH,
                      TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_PATH) == FSE_OK;
    }
    if(!success) storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_TMP_PATH);
    return success;
}

static const char* tumoflip_runtime_radio_device_name(SubGhzRadioBrokerDevice device) {
    switch(device) {
    case SubGhzRadioBrokerDeviceInternal:
        return "internal";
    case SubGhzRadioBrokerDeviceExternalCC1101:
        return "external";
    case SubGhzRadioBrokerDeviceDual:
        return "dual";
    default:
        return "unknown";
    }
}

static void tumoflip_runtime_cli_print_radio_caps(TumoflipRuntime* runtime) {
    printf("RADIO_CAPS schema=1\r\n");
    for(uint8_t device = SubGhzRadioBrokerDeviceInternal;
        device <= SubGhzRadioBrokerDeviceDual;
        device++) {
        SubGhzRadioBrokerCapability capability;
        if(!subghz_radio_broker_get_capability(
               runtime->radio_broker, (SubGhzRadioBrokerDevice)device, &capability)) {
            printf("RADIO_CAP device=%s;status=unsupported\r\n", tumoflip_runtime_radio_device_name(device));
            continue;
        }
        printf(
            "RADIO_CAP device=%s;rx=%u;tx=%u;power=%u;rx_range=%lu-%lu;tx_range=%lu-%lu\r\n",
            tumoflip_runtime_radio_device_name(capability.device),
            capability.can_receive ? 1U : 0U,
            capability.can_transmit ? 1U : 0U,
            capability.requires_external_power ? 1U : 0U,
            (unsigned long)capability.rx_min_frequency,
            (unsigned long)capability.rx_max_frequency,
            (unsigned long)capability.tx_min_frequency,
            (unsigned long)capability.tx_max_frequency);
    }
}

static void tumoflip_runtime_cli_print_radio_sessions(TumoflipRuntime* runtime) {
    SubGhzRadioBrokerSessionRecord records[SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH];
    const size_t count = subghz_radio_broker_session_history(
        runtime->radio_broker, records, COUNT_OF(records));
    printf("RADIO_SESSIONS schema=1;count=%u\r\n", (unsigned)count);
    for(size_t index = 0U; index < count; index++) {
        const SubGhzRadioBrokerSessionRecord* record = &records[index];
        printf(
            "RADIO_SESSION id=%08lX;active=%u;state=%s;source=%s;device=%s;seq=%lu;freq=%lu;"
            "rssi=%d;protocol=%s;mod=%s;policy=%s;started=%lu;closed=%lu\r\n",
            (unsigned long)record->session_id,
            record->active ? 1U : 0U,
            tumoflip_runtime_radio_session_state_name(record->state),
            record->source,
            tumoflip_runtime_radio_device_name(record->device),
            (unsigned long)record->sequence,
            (unsigned long)record->frequency,
            record->rssi_dbm,
            record->protocol[0] ? record->protocol : "unknown",
            record->modulation[0] ? record->modulation : "unknown",
            record->policy[0] ? record->policy : "unknown",
            (unsigned long)record->started_at,
            (unsigned long)record->closed_at);
    }
}

static void tumoflip_runtime_sanitize_csv_token(
    const char* input,
    char* output,
    size_t output_size) {
    if(!output || output_size == 0U) return;
    size_t offset = 0U;
    if(input) {
        for(const char* cursor = input; *cursor && offset + 1U < output_size; cursor++) {
            const char value = *cursor;
            output[offset++] = (value == ',' || value == '"' || value == '\r' || value == '\n') ?
                                   '_' :
                                   value;
        }
    }
    output[offset] = '\0';
}

static bool tumoflip_runtime_radio_sessions_export(TumoflipRuntime* runtime) {
    if(!runtime) return false;
    if(storage_simply_mkdir(runtime->storage, EXT_PATH(".tumoflip")) == false) return false;

    SubGhzRadioBrokerSessionRecord records[SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH];
    const size_t count = subghz_radio_broker_session_history(
        runtime->radio_broker, records, COUNT_OF(records));
    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_RADIO_SESSIONS_TMP_PATH);
    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_RADIO_SESSIONS_BAK_PATH);

    File* file = storage_file_alloc(runtime->storage);
    bool success = storage_file_open(
        file, TUMOFLIP_RUNTIME_RADIO_SESSIONS_TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        static const char header[] =
            "schema,session_id,state,active,source,device,sequence,started_at,closed_at,"
            "frequency,rssi_dbm,protocol,modulation,policy\n";
        success = storage_file_write(file, header, sizeof(header) - 1U) == sizeof(header) - 1U;
        for(size_t index = 0U; success && index < count; index++) {
            const SubGhzRadioBrokerSessionRecord* record = &records[index];
            char source[SUBGHZ_RADIO_BROKER_SESSION_SOURCE_MAX + 1U];
            char protocol[SUBGHZ_RADIO_BROKER_SESSION_PROTOCOL_MAX + 1U];
            char modulation[SUBGHZ_RADIO_BROKER_SESSION_MODULATION_MAX + 1U];
            char policy[SUBGHZ_RADIO_BROKER_SESSION_POLICY_MAX + 1U];
            char line[384];
            tumoflip_runtime_sanitize_csv_token(record->source, source, sizeof(source));
            tumoflip_runtime_sanitize_csv_token(record->protocol, protocol, sizeof(protocol));
            tumoflip_runtime_sanitize_csv_token(record->modulation, modulation, sizeof(modulation));
            tumoflip_runtime_sanitize_csv_token(record->policy, policy, sizeof(policy));
            const int written = snprintf(
                line,
                sizeof(line),
                "1,%08lX,%s,%u,%s,%s,%lu,%lu,%lu,%lu,%d,%s,%s,%s\n",
                (unsigned long)record->session_id,
                tumoflip_runtime_radio_session_state_name(record->state),
                record->active ? 1U : 0U,
                source[0] ? source : "unknown",
                tumoflip_runtime_radio_device_name(record->device),
                (unsigned long)record->sequence,
                (unsigned long)record->started_at,
                (unsigned long)record->closed_at,
                (unsigned long)record->frequency,
                record->rssi_dbm,
                protocol[0] ? protocol : "unknown",
                modulation[0] ? modulation : "unknown",
                policy[0] ? policy : "unknown");
            success = written > 0 && (size_t)written < sizeof(line) &&
                      storage_file_write(file, line, (size_t)written) == (size_t)written;
        }
        success = success && storage_file_sync(file);
        storage_file_close(file);
    } else {
        storage_file_close(file);
    }
    storage_file_free(file);

    if(!success) {
        storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_RADIO_SESSIONS_TMP_PATH);
        return false;
    }

    bool backup_moved = false;
    if(storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_RADIO_SESSIONS_PATH)) {
        if(storage_common_rename(
               runtime->storage,
               TUMOFLIP_RUNTIME_RADIO_SESSIONS_PATH,
               TUMOFLIP_RUNTIME_RADIO_SESSIONS_BAK_PATH) != FSE_OK) {
            storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_RADIO_SESSIONS_TMP_PATH);
            return false;
        }
        backup_moved = true;
    }

    if(storage_common_rename(
           runtime->storage,
           TUMOFLIP_RUNTIME_RADIO_SESSIONS_TMP_PATH,
           TUMOFLIP_RUNTIME_RADIO_SESSIONS_PATH) != FSE_OK) {
        if(backup_moved) {
            storage_common_rename(
                runtime->storage,
                TUMOFLIP_RUNTIME_RADIO_SESSIONS_BAK_PATH,
                TUMOFLIP_RUNTIME_RADIO_SESSIONS_PATH);
        }
        storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_RADIO_SESSIONS_TMP_PATH);
        return false;
    }

    storage_simply_remove(runtime->storage, TUMOFLIP_RUNTIME_RADIO_SESSIONS_BAK_PATH);
    return true;
}

static void tumoflip_runtime_cli_print_radio_protocols(TumoflipRuntime* runtime) {
    static const char* const protocols[] = {"RAW", "ProtoPirate", "Keeloq"};
    printf("RADIO_PROTOCOLS schema=1;count=%u\r\n", (unsigned)COUNT_OF(protocols));
    for(size_t index = 0U; index < COUNT_OF(protocols); index++) {
        SubGhzRadioBrokerProtocolCapability capability;
        const bool supported = subghz_radio_broker_get_protocol_capability(
            runtime->radio_broker,
            protocols[index],
            SubGhzRadioBrokerDeviceInternal,
            &capability);
        printf(
            "RADIO_PROTOCOL protocol=%s;status=%s;rx=%u;tx=%u;device=%s;mod=%s;bw=%s;preset=%s;"
            "reason=%s;range=%lu-%lu\r\n",
            protocols[index],
            supported && capability.supported ? "supported" : "unsupported",
            supported && capability.can_receive ? 1U : 0U,
            supported && capability.can_transmit ? 1U : 0U,
            supported ? tumoflip_runtime_radio_device_name(capability.device) : "unknown",
            supported ? capability.modulation : "unknown",
            supported ? capability.bandwidth : "unknown",
            supported ? capability.preset : "unknown",
            supported && capability.reason[0] ? capability.reason : "none",
            supported ? (unsigned long)capability.rx_min_frequency : 0UL,
            supported ? (unsigned long)capability.rx_max_frequency : 0UL);
    }
}

static void tumoflip_runtime_make_radio_protocols_payload(
    TumoflipRuntime* runtime,
    char* payload,
    size_t size) {
    static const char* const protocols[] = {"RAW", "ProtoPirate", "Keeloq"};
    size_t used = 0U;
    int written = snprintf(payload, size, "schema=1;count=%u", (unsigned)COUNT_OF(protocols));
    if(written < 0 || (size_t)written >= size) {
        if(size) payload[0] = '\0';
        return;
    }
    used = (size_t)written;
    for(size_t index = 0U; index < COUNT_OF(protocols); index++) {
        SubGhzRadioBrokerProtocolCapability capability;
        const bool supported = subghz_radio_broker_get_protocol_capability(
            runtime->radio_broker,
            protocols[index],
            SubGhzRadioBrokerDeviceInternal,
            &capability);
        written = snprintf(
            &payload[used],
            size - used,
            ";p=%s,%u,%u,%s,%lu-%lu",
            protocols[index],
            supported && capability.can_receive ? 1U : 0U,
            supported && capability.can_transmit ? 1U : 0U,
            supported ? capability.modulation : "unknown",
            supported ? (unsigned long)capability.rx_min_frequency : 0UL,
            supported ? (unsigned long)capability.rx_max_frequency : 0UL);
        if(written < 0 || (size_t)written >= size - used) {
            payload[used] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

static void tumoflip_runtime_make_radio_caps_payload(
    TumoflipRuntime* runtime,
    char* payload,
    size_t size) {
    SubGhzRadioBrokerCapability internal;
    SubGhzRadioBrokerCapability external;
    const bool internal_ok = subghz_radio_broker_get_capability(
        runtime->radio_broker, SubGhzRadioBrokerDeviceInternal, &internal);
    const bool external_ok = subghz_radio_broker_get_capability(
        runtime->radio_broker, SubGhzRadioBrokerDeviceExternalCC1101, &external);
    snprintf(
        payload,
        size,
        "schema=1;internal=%u,%u,%lu-%lu;external=%u,%u,%lu-%lu;external_power=1",
        internal_ok && internal.can_receive ? 1U : 0U,
        internal_ok && internal.can_transmit ? 1U : 0U,
        internal_ok ? (unsigned long)internal.tx_min_frequency : 0UL,
        internal_ok ? (unsigned long)internal.tx_max_frequency : 0UL,
        external_ok && external.can_receive ? 1U : 0U,
        external_ok && external.can_transmit ? 1U : 0U,
        external_ok ? (unsigned long)external.tx_min_frequency : 0UL,
        external_ok ? (unsigned long)external.tx_max_frequency : 0UL);
}

static void tumoflip_runtime_make_radio_sessions_payload(
    TumoflipRuntime* runtime,
    char* payload,
    size_t size) {
    SubGhzRadioBrokerSessionRecord records[SUBGHZ_RADIO_BROKER_SESSION_HISTORY_DEPTH];
    const size_t count = subghz_radio_broker_session_history(
        runtime->radio_broker, records, COUNT_OF(records));
    if(count == 0U) {
        snprintf(payload, size, "schema=1;count=0");
        return;
    }
    const SubGhzRadioBrokerSessionRecord* latest = &records[count - 1U];
    snprintf(
        payload,
        size,
        "schema=1;count=%u;active=%u;id=%08lX;source=%s;device=%s;seq=%lu;freq=%lu",
        (unsigned)count,
        latest->active ? 1U : 0U,
        (unsigned long)latest->session_id,
        latest->source,
        tumoflip_runtime_radio_device_name(latest->device),
        (unsigned long)latest->sequence,
        (unsigned long)latest->frequency);
}

static void tumoflip_runtime_make_journal_payload(
    TumoflipRuntime* runtime,
    char* payload,
    size_t size) {
    const TumoflipRuntimeJournal* journal = &runtime->journal;
    snprintf(
        payload,
        size,
        "schema=1;sequence=%lu;kind=%s;timestamp=%lu;reset=0x%08lX;fault=0x%08lX;"
        "app=%s;scene=%s;commit=%s",
        (unsigned long)journal->sequence,
        tumoflip_runtime_journal_kind_name(journal->kind),
        (unsigned long)journal->timestamp,
        (unsigned long)journal->reset_flags,
        (unsigned long)journal->fault_data,
        journal->app_id[0] ? journal->app_id : "unknown",
        journal->scene[0] ? journal->scene : "unknown",
        journal->firmware_commit[0] ? journal->firmware_commit : "unknown");
}

static void tumoflip_runtime_cli(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(pipe);
    TumoflipRuntime* runtime = context;
    FuriString* verb = furi_string_alloc();

    if(!runtime || !args_read_string_and_trim(args, verb)) {
        tumoflip_runtime_cli_print_error("usage");
        goto out;
    }

    const char* command = furi_string_get_cstr(verb);
    if(strcmp(command, "caps") == 0) {
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else {
            TumoflipRuntimeFabricSnapshot snapshot;
            if(tumoflip_runtime_api_get_fabric_state(&runtime->api, &snapshot)) {
                printf(
                    "FABRIC schema=1;status=ok;node=flipper;transport=usb;"
                    "ops=state,start,inc,dec,cancel,trace;active=%u;owner=%s\r\n",
                    snapshot.active ? 1U : 0U,
                    snapshot.active ? snapshot.owner : "none");
            } else {
                tumoflip_runtime_cli_print_error("runtime");
            }
        }
    } else if(strcmp(command, "state") == 0) {
        if(args_length(args) == 0U) {
            tumoflip_runtime_cli_print_snapshot(runtime);
        } else {
            tumoflip_runtime_cli_print_error("args");
        }
    } else if(strcmp(command, "start") == 0) {
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else if(!tumoflip_runtime_api_open_local_fabric(&runtime->api)) {
            tumoflip_runtime_cli_print_error("busy");
        } else {
            tumoflip_runtime_trace_add(runtime, 's', "fabric_usb_start", false);
            tumoflip_runtime_cli_print_snapshot(runtime);
        }
    } else if(strcmp(command, "step") == 0) {
        FuriString* operation = furi_string_alloc();
        const bool has_operation = args_read_string_and_trim(args, operation);
        const bool has_extra = args_length(args) != 0U;
        int8_t delta = 0;
        if(has_operation && !has_extra) {
            if(furi_string_cmp_str(operation, "inc") == 0) {
                delta = 1;
            } else if(furi_string_cmp_str(operation, "dec") == 0) {
                delta = -1;
            }
        }

        if(delta == 0) {
            tumoflip_runtime_cli_print_error("args");
        } else if(!tumoflip_runtime_api_step_local_fabric(&runtime->api, delta)) {
            tumoflip_runtime_cli_print_error("state");
        } else {
            tumoflip_runtime_trace_add(runtime, 's', "fabric_usb_step", false);
            tumoflip_runtime_cli_print_snapshot(runtime);
        }
        furi_string_free(operation);
    } else if(strcmp(command, "cancel") == 0) {
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else if(!tumoflip_runtime_api_cancel_fabric(&runtime->api)) {
            tumoflip_runtime_cli_print_error("runtime");
        } else {
            tumoflip_runtime_cli_print_snapshot(runtime);
        }
    } else if(strcmp(command, "trace") == 0) {
        char trace[TUMOFLIP_RUNTIME_TRACE_MAX];
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else if(!tumoflip_runtime_api_get_trace(&runtime->api, trace, sizeof(trace))) {
            tumoflip_runtime_cli_print_error("runtime");
        } else {
            printf("FABRIC schema=1;status=ok;trace=%s\r\n", trace);
        }
    } else if(strcmp(command, "diagnostics") == 0 || strcmp(command, "run") == 0) {
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else {
            tumoflip_runtime_cli_print_diagnostics(runtime);
        }
    } else if(strcmp(command, "diagnostics_export") == 0) {
        TumoflipRuntimeDiagnosticReport report;
        if(args_length(args) != 0U || !tumoflip_runtime_api_get_diagnostics(&runtime->api, &report) ||
           !tumoflip_runtime_diagnostics_export(runtime, &report)) {
            tumoflip_runtime_cli_print_error("diagnostics");
        } else {
            printf("DIAG export=ok;path=%s\r\n", TUMOFLIP_RUNTIME_DIAGNOSTIC_REPORT_PATH);
        }
    } else if(strcmp(command, "radio_caps") == 0) {
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else {
            tumoflip_runtime_cli_print_radio_caps(runtime);
        }
    } else if(strcmp(command, "radio_sessions") == 0) {
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else {
            tumoflip_runtime_cli_print_radio_sessions(runtime);
        }
    } else if(strcmp(command, "radio_sessions_export") == 0) {
        if(args_length(args) != 0U || !tumoflip_runtime_radio_sessions_export(runtime)) {
            tumoflip_runtime_cli_print_error("radio_sessions_export");
        } else {
            printf(
                "RADIO_SESSIONS export=ok;path=%s\r\n",
                TUMOFLIP_RUNTIME_RADIO_SESSIONS_PATH);
        }
    } else if(strcmp(command, "radio_protocols") == 0) {
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else {
            tumoflip_runtime_cli_print_radio_protocols(runtime);
        }
    } else if(strcmp(command, "journal") == 0) {
        char payload[TUMOFLIP_RUNTIME_JOURNAL_MAX];
        if(args_length(args) != 0U) {
            tumoflip_runtime_cli_print_error("args");
        } else {
            tumoflip_runtime_make_journal_payload(runtime, payload, sizeof(payload));
            printf("JOURNAL %s\r\n", payload);
        }
    } else if(strcmp(command, "journal_clear") == 0) {
        if(args_length(args) != 0U || !tumoflip_runtime_journal_clear(runtime)) {
            tumoflip_runtime_cli_print_error("journal");
        } else {
            printf("JOURNAL schema=1;status=cleared\r\n");
        }
    } else {
        tumoflip_runtime_cli_print_error("verb");
    }

out:
    furi_string_free(verb);
}

static void tumoflip_runtime_bridge_callback(const void* message, void* context) {
    TumoflipRuntime* runtime = context;
    const BtAppBridgeEvent* event = message;

    if((event->protocol_version != 2) || (strcmp(event->app_id, TUMOFLIP_RUNTIME_APP_ID) != 0) ||
       (event->flags & BtAppBridgeFlagResponse)) {
        return;
    }

    const TumoflipRuntimeMessage runtime_message = {.event = *event};
    furi_message_queue_put(runtime->queue, &runtime_message, 0);
}

static void tumoflip_runtime_reply(
    TumoflipRuntime* runtime,
    uint32_t request_id,
    const char* command,
    const char* payload,
    bool error) {
    const uint8_t flags = BtAppBridgeFlagResponse | (error ? BtAppBridgeFlagError : 0);
    bt_app_bridge_send_text_v2(
        runtime->bt, TUMOFLIP_RUNTIME_APP_ID, command, request_id, flags, payload);
    tumoflip_runtime_trace_add(runtime, error ? 'e' : 't', command, error);
}

static const char* tumoflip_runtime_str_or_unknown(const char* value) {
    return value ? value : "unknown";
}

static void tumoflip_runtime_transfer_activity(TumoflipRuntime* runtime, bool active) {
    runtime->transfer_active = active;
    BtMessage message = {
        .lock = api_lock_alloc_locked(),
        .type = BtMessageTypeTransferActivity,
        .data.transfer_active = active,
    };
    furi_check(
        furi_message_queue_put(runtime->bt->message_queue, &message, FuriWaitForever) ==
        FuriStatusOk);
    api_lock_wait_unlock_and_free(message.lock);
}

static void tumoflip_runtime_trace_add(
    TumoflipRuntime* runtime,
    char code,
    const char* command,
    bool error) {
    furi_check(furi_mutex_acquire(runtime->trace_mutex, FuriWaitForever) == FuriStatusOk);
    TumoflipRuntimeTraceEvent* trace = &runtime->trace[runtime->trace_head];
    trace->code = code;
    trace->command = (command && command[0]) ? command[0] : '-';
    trace->error = error;

    runtime->trace_head = (runtime->trace_head + 1U) % TUMOFLIP_RUNTIME_TRACE_DEPTH;
    if(runtime->trace_count < TUMOFLIP_RUNTIME_TRACE_DEPTH) {
        runtime->trace_count++;
    } else {
        runtime->trace_dropped++;
    }
    furi_check(furi_mutex_release(runtime->trace_mutex) == FuriStatusOk);
}

static void
    tumoflip_runtime_make_trace_payload(TumoflipRuntime* runtime, char* payload, size_t size) {
    furi_check(furi_mutex_acquire(runtime->trace_mutex, FuriWaitForever) == FuriStatusOk);
    int written = snprintf(
        payload,
        size,
        "schema=1;depth=%u;count=%u;drop=%lu",
        TUMOFLIP_RUNTIME_TRACE_DEPTH,
        runtime->trace_count,
        (unsigned long)runtime->trace_dropped);
    if(written < 0) goto out;
    size_t used = (size_t)written;
    if(used >= size) goto out;

    for(uint8_t offset = 0; offset < runtime->trace_count; offset++) {
        const uint8_t index =
            (runtime->trace_head + TUMOFLIP_RUNTIME_TRACE_DEPTH - runtime->trace_count + offset) %
            TUMOFLIP_RUNTIME_TRACE_DEPTH;
        const TumoflipRuntimeTraceEvent* trace = &runtime->trace[index];

        written = snprintf(
            &payload[used],
            size - used,
            "|%c,%c,%c",
            trace->code,
            trace->command,
            trace->error ? 'e' : 'o');

        if(written < 0) break;
        used += (size_t)written;
        if(used >= size) break;
    }

out:
    furi_check(furi_mutex_release(runtime->trace_mutex) == FuriStatusOk);
}

static bool
    tumoflip_runtime_api_get_trace(TumoflipRuntimeApi* api, char* output, size_t output_size) {
    furi_assert(api);
    furi_assert(output);
    TumoflipRuntime* runtime = api->context;
    if(!runtime || output_size == 0U) {
        return false;
    }

    tumoflip_runtime_make_trace_payload(runtime, output, output_size);
    output[output_size - 1U] = '\0';
    return true;
}

static bool tumoflip_runtime_api_get_fabric_state(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeFabricSnapshot* snapshot) {
    furi_assert(api);
    furi_assert(snapshot);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    snapshot->active = runtime->fabric.active;
    snapshot->session_id = runtime->fabric.session_id;
    snapshot->token = runtime->fabric.token;
    snapshot->last_sequence = runtime->fabric.last_sequence;
    snapshot->counter = runtime->fabric.counter;
    strlcpy(snapshot->owner, runtime->fabric.owner, sizeof(snapshot->owner));
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    return true;
}

static bool tumoflip_runtime_api_cancel_fabric(TumoflipRuntimeApi* api) {
    furi_assert(api);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    tumofabric_init(&runtime->fabric);
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    tumoflip_runtime_trace_add(runtime, 's', "fabric_cancel", false);
    return true;
}

static bool tumoflip_runtime_api_open_local_fabric(TumoflipRuntimeApi* api) {
    furi_assert(api);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    const bool opened = tumofabric_open_local(&runtime->fabric);
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    return opened;
}

static bool tumoflip_runtime_api_step_local_fabric(TumoflipRuntimeApi* api, int8_t delta) {
    furi_assert(api);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    const bool stepped = tumofabric_step_local(&runtime->fabric, delta);
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    return stepped;
}

static bool tumoflip_runtime_api_get_diagnostics(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeDiagnosticReport* report) {
    return tumoflip_runtime_diagnostics_run(api, report);
}

static void
    tumoflip_runtime_handle_fabric(TumoflipRuntime* runtime, const BtAppBridgeEvent* event) {
    char response[TUMOFLIP_RUNTIME_FABRIC_MAX];
    TumoFabricResult result = TumoFabricResultPayload;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    if(strcmp(event->command, "fabric_open") == 0) {
        result = tumofabric_open(
            &runtime->fabric,
            event->request_id,
            event->payload,
            event->payload_len,
            response,
            sizeof(response));
    } else if(strcmp(event->command, "fabric_state") == 0) {
        result = tumofabric_get_state(
            &runtime->fabric, event->payload, event->payload_len, response, sizeof(response));
    } else if(strcmp(event->command, "fabric_step") == 0) {
        result = tumofabric_step(
            &runtime->fabric, event->payload, event->payload_len, response, sizeof(response));
    } else if(strcmp(event->command, "fabric_cancel") == 0) {
        result = tumofabric_cancel(
            &runtime->fabric, event->payload, event->payload_len, response, sizeof(response));
    }
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);

    if(result == TumoFabricResultOk) {
        tumoflip_runtime_reply(runtime, event->request_id, event->command, response, false);
    } else {
        tumoflip_runtime_reply(
            runtime, event->request_id, "error", tumofabric_result_token(result), true);
    }
}

static void
    tumoflip_runtime_make_status_payload(TumoflipRuntime* runtime, char* payload, size_t size) {
    const Version* version = furi_hal_version_get_firmware_version();
    uint16_t api_major = 0;
    uint16_t api_minor = 0;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    const uint8_t dirty = version_get_dirty_flag(version) ? 1U : 0U;
    const uint8_t target = version_get_target(version);
    const uint8_t transfer_active = runtime->transfer_active ? 1U : 0U;
    const uint8_t sd_ready = (storage_sd_status(runtime->storage) == FSE_OK) ? 1U : 0U;
    const uint8_t package_state =
        (sd_ready && storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH)) ?
            1U :
            0U;

    SubGhzRadioBrokerStatusInternal radio_status;
    subghz_radio_broker_get_status_internal(runtime->radio_broker, &radio_status);

    snprintf(
        payload,
        size,
        "schema=2;fw=%.8s;commit=%.8s;dirty=%hhu;origin=%.4s;api=%hu.%hu;target=%hhu;"
        "transfer=%hhu;sd=%hhu;pkg=%hhu;sid=%08lX;bo=%.8s;radio=%hhu;owner=%.4s",
        tumoflip_runtime_str_or_unknown(version_get_version(version)),
        tumoflip_runtime_str_or_unknown(version_get_githash(version)),
        dirty,
        tumoflip_runtime_str_or_unknown(version_get_firmware_origin(version)),
        api_major,
        api_minor,
        target,
        transfer_active,
        sd_ready,
        package_state,
        (unsigned long)runtime->session.session_id,
        runtime->session.owner,
        (uint8_t)radio_status.state,
        radio_status.base.owner);
}

static void
    tumoflip_runtime_make_twin_payload(TumoflipRuntime* runtime, char* payload, size_t size) {
    const Version* version = furi_hal_version_get_firmware_version();
    const uint8_t dirty = version_get_dirty_flag(version) ? 1U : 0U;
    const uint8_t sd_ready = (storage_sd_status(runtime->storage) == FSE_OK) ? 1U : 0U;
    const uint8_t package_state =
        (sd_ready && storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH)) ?
            1U :
            0U;
    const uint8_t charging = furi_hal_power_is_charging() ? 1U : 0U;
    const uint8_t otg = furi_hal_power_is_otg_enabled() ? 1U : 0U;

    SubGhzRadioBrokerStatusV2 radio_status;
    subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status);

    snprintf(
        payload,
        size,
        "schema=1;fw=%.8s;cm=%.8s;dy=%hhu;sd=%hhu;pkg=%hhu;bat=%u;chg=%hhu;otg=%hhu;"
        "heap=%lu;rf=%hhu;ro=%.4s;sid=%08lX;bo=%.8s",
        tumoflip_runtime_str_or_unknown(version_get_version(version)),
        tumoflip_runtime_str_or_unknown(version_get_githash(version)),
        dirty,
        sd_ready,
        package_state,
        furi_hal_power_get_pct(),
        charging,
        otg,
        (unsigned long)memmgr_heap_get_max_free_block(),
        (uint8_t)radio_status.state,
        radio_status.base.owner,
        (unsigned long)runtime->session.session_id,
        runtime->session.owner);
}

static void tumoflip_runtime_make_fabric_capabilities_payload(
    TumoflipRuntime* runtime,
    char* payload,
    size_t size) {
    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = runtime->fabric.active;
    const char* owner = active ? runtime->fabric.owner : "none";
    snprintf(
        payload,
        size,
        "schema=1;node=flipper;pkg=counter;ops=inc,dec;resume=1;persist=ram;"
        "trust=ble-bond;active=%u;owner=%s",
        active ? 1U : 0U,
        owner);
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
}

static void
    tumoflip_runtime_handle_request(TumoflipRuntime* runtime, const BtAppBridgeEvent* event) {
    const char* command = event->command;
    const uint8_t* payload = event->payload;
    const size_t payload_len = event->payload_len;

    if((event->chunk_index != 0U) || (event->chunk_count != 1U)) {
        tumoflip_runtime_trace_add(runtime, 'e', command, true);
        tumoflip_runtime_reply(runtime, event->request_id, "error", "chunk", true);
        return;
    }

    tumoflip_runtime_trace_add(runtime, 'r', command, false);

    if(strcmp(command, "ping") == 0) {
        tumoflip_runtime_reply(runtime, event->request_id, "pong", "ok", false);
    } else if(strcmp(command, "capabilities") == 0) {
        tumoflip_runtime_reply(
            runtime, event->request_id, "capabilities", TUMOFLIP_RUNTIME_CAPABILITIES, false);
    } else if(strcmp(command, "status") == 0) {
        char payload[TUMOFLIP_RUNTIME_STATUS_MAX];
        tumoflip_runtime_make_status_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "status", payload, false);
    } else if(strcmp(command, "trace") == 0) {
        char payload[TUMOFLIP_RUNTIME_TRACE_MAX];
        tumoflip_runtime_make_trace_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "trace", payload, false);
    } else if(strcmp(command, "twin") == 0) {
        char payload[TUMOFLIP_RUNTIME_TWIN_MAX];
        tumoflip_runtime_make_twin_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "twin", payload, false);
    } else if(strcmp(command, "diagnostics") == 0) {
        TumoflipRuntimeDiagnosticReport report;
        char payload[TUMOFLIP_RUNTIME_DIAGNOSTIC_MAX];
        if(payload_len != 0U || !tumoflip_runtime_api_get_diagnostics(&runtime->api, &report) ||
           !tumoflip_runtime_diagnostics_format(&report, payload, sizeof(payload))) {
            tumoflip_runtime_reply(runtime, event->request_id, "error", "diagnostics", true);
        } else {
            tumoflip_runtime_diagnostics_export(runtime, &report);
            tumoflip_runtime_reply(runtime, event->request_id, "diagnostics", payload, false);
        }
    } else if(strcmp(command, "diagnostics_export") == 0) {
        TumoflipRuntimeDiagnosticReport report;
        if(payload_len != 0U || !tumoflip_runtime_api_get_diagnostics(&runtime->api, &report) ||
           !tumoflip_runtime_diagnostics_export(runtime, &report)) {
            tumoflip_runtime_reply(runtime, event->request_id, "error", "diagnostics", true);
        } else {
            tumoflip_runtime_reply(
                runtime,
                event->request_id,
                "diagnostics_export",
                "path=/.tumoflip/diagnostics-last.txt",
                false);
        }
    } else if(strcmp(command, "radio_caps") == 0) {
        char payload[TUMOFLIP_RUNTIME_RADIO_MAX];
        if(payload_len != 0U) {
            tumoflip_runtime_reply(runtime, event->request_id, "error", "radio_caps", true);
        } else {
            tumoflip_runtime_make_radio_caps_payload(runtime, payload, sizeof(payload));
            tumoflip_runtime_reply(runtime, event->request_id, "radio_caps", payload, false);
        }
    } else if(strcmp(command, "radio_sessions") == 0) {
        char payload[TUMOFLIP_RUNTIME_RADIO_MAX];
        if(payload_len != 0U) {
            tumoflip_runtime_reply(runtime, event->request_id, "error", "radio_sessions", true);
        } else {
            tumoflip_runtime_make_radio_sessions_payload(runtime, payload, sizeof(payload));
            tumoflip_runtime_reply(runtime, event->request_id, "radio_sessions", payload, false);
        }
    } else if(strcmp(command, "radio_sessions_export") == 0) {
        if(payload_len != 0U || !tumoflip_runtime_radio_sessions_export(runtime)) {
            tumoflip_runtime_reply(
                runtime, event->request_id, "error", "radio_sessions_export", true);
        } else {
            tumoflip_runtime_reply(
                runtime,
                event->request_id,
                "radio_sessions_export",
                "path=/.tumoflip/radio-sessions.csv",
                false);
        }
    } else if(strcmp(command, "radio_protocols") == 0) {
        char payload[TUMOFLIP_RUNTIME_RADIO_PROTOCOL_MAX];
        if(payload_len != 0U) {
            tumoflip_runtime_reply(runtime, event->request_id, "error", "radio_protocols", true);
        } else {
            tumoflip_runtime_make_radio_protocols_payload(runtime, payload, sizeof(payload));
            tumoflip_runtime_reply(runtime, event->request_id, "radio_protocols", payload, false);
        }
    } else if(strcmp(command, "journal") == 0) {
        char payload[TUMOFLIP_RUNTIME_JOURNAL_MAX];
        if(payload_len != 0U) {
            tumoflip_runtime_reply(runtime, event->request_id, "error", "journal", true);
        } else {
            tumoflip_runtime_make_journal_payload(runtime, payload, sizeof(payload));
            tumoflip_runtime_reply(runtime, event->request_id, "journal", payload, false);
        }
    } else if(strcmp(command, "fabric_caps") == 0) {
        char payload[TUMOFLIP_RUNTIME_FABRIC_MAX];
        tumoflip_runtime_make_fabric_capabilities_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "fabric_caps", payload, false);
    } else if(
        strcmp(command, "fabric_open") == 0 || strcmp(command, "fabric_state") == 0 ||
        strcmp(command, "fabric_step") == 0 || strcmp(command, "fabric_cancel") == 0) {
        tumoflip_runtime_handle_fabric(runtime, event);
    } else if(strcmp(command, "transfer_begin") == 0) {
        tumoflip_runtime_transfer_activity(runtime, true);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_begin", "ok", false);
    } else if(strcmp(command, "transfer_progress") == 0) {
        tumoflip_runtime_transfer_activity(runtime, true);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_progress", "ok", false);
    } else if(strcmp(command, "transfer_end") == 0) {
        tumoflip_runtime_transfer_activity(runtime, false);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_end", "ok", false);
    } else if(strcmp(command, "hello") == 0) {
        const size_t owner_prefix_len = 6U;
        size_t owner_len = 0U;
        bool owner_valid = (payload_len > owner_prefix_len) &&
                           (memcmp(payload, "owner=", owner_prefix_len) == 0);
        if(owner_valid) {
            owner_len = payload_len - owner_prefix_len;
            owner_valid = owner_len <= TUMOFLIP_RUNTIME_SESSION_OWNER_MAX;
        }

        if(!owner_valid) {
            tumoflip_runtime_trace_add(runtime, 'e', "hello", true);
            tumoflip_runtime_reply(runtime, event->request_id, "error", "owner", true);
        } else {
            runtime->session.session_id = event->request_id;
            memcpy(runtime->session.owner, &payload[owner_prefix_len], owner_len);
            runtime->session.owner[owner_len] = '\0';
            tumoflip_runtime_trace_add(runtime, 's', runtime->session.owner, false);

            char response[48];
            snprintf(
                response,
                sizeof(response),
                "sid=%08lX",
                (unsigned long)runtime->session.session_id);
            tumoflip_runtime_reply(runtime, event->request_id, "hello", response, false);
        }
    } else {
        tumoflip_runtime_trace_add(runtime, 'e', command, true);
        tumoflip_runtime_reply(runtime, event->request_id, "error", "badcmd", true);
    }
}

int32_t tumoflip_runtime_srv(void* context) {
    UNUSED(context);

    TumoflipRuntime* runtime = malloc(sizeof(TumoflipRuntime));
    memset(runtime, 0, sizeof(TumoflipRuntime));
    runtime->queue =
        furi_message_queue_alloc(TUMOFLIP_RUNTIME_QUEUE_DEPTH, sizeof(TumoflipRuntimeMessage));
    runtime->trace_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    runtime->fabric_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    runtime->api.get_trace = tumoflip_runtime_api_get_trace;
    runtime->api.get_fabric_state = tumoflip_runtime_api_get_fabric_state;
    runtime->api.cancel_fabric = tumoflip_runtime_api_cancel_fabric;
    runtime->api.open_local_fabric = tumoflip_runtime_api_open_local_fabric;
    runtime->api.step_local_fabric = tumoflip_runtime_api_step_local_fabric;
    runtime->api.get_diagnostics = tumoflip_runtime_api_get_diagnostics;
    runtime->api.context = runtime;
    tumofabric_init(&runtime->fabric);
    runtime->bt = furi_record_open(RECORD_BT);
    runtime->storage = furi_record_open(RECORD_STORAGE);
    runtime->radio_broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    tumoflip_runtime_journal_init(runtime);
    furi_record_create(RECORD_TUMOFLIP_RUNTIME, &runtime->api);

    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_add_command_ex(
        cli, "tumofabric", CliCommandFlagParallelSafe, tumoflip_runtime_cli, runtime, 1536U);
    cli_registry_add_command_ex(
        cli, "tumodiag", CliCommandFlagParallelSafe, tumoflip_runtime_cli, runtime, 1536U);
    furi_record_close(RECORD_CLI);

    runtime->subscription = furi_pubsub_subscribe(
        bt_app_bridge_get_pubsub(runtime->bt), tumoflip_runtime_bridge_callback, runtime);

    TumoflipRuntimeMessage message;
    while(true) {
        if(furi_message_queue_get(runtime->queue, &message, FuriWaitForever) == FuriStatusOk) {
            tumoflip_runtime_handle_request(runtime, &message.event);
        }
    }

    return 0;
}
