#pragma once

#include <furi.h>
#include <stddef.h>
#include <stdint.h>

#define RECORD_TUMOFLIP_RUNTIME "tumoflip_runtime"

typedef struct TumoflipRuntimeApi TumoflipRuntimeApi;

#define TUMOFLIP_RUNTIME_FABRIC_OWNER_MAX 16U
#define TUMOFLIP_RUNTIME_DIAGNOSTIC_DETAIL_MAX 56U
#define TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT 10U

typedef struct {
    bool active;
    uint32_t session_id;
    uint32_t token;
    uint32_t last_sequence;
    int16_t counter;
    char owner[TUMOFLIP_RUNTIME_FABRIC_OWNER_MAX + 1U];
} TumoflipRuntimeFabricSnapshot;

typedef enum {
    TumoflipRuntimeDiagnosticCheckIdentity,
    TumoflipRuntimeDiagnosticCheckHeap,
    TumoflipRuntimeDiagnosticCheckStorage,
    TumoflipRuntimeDiagnosticCheckBattery,
    TumoflipRuntimeDiagnosticCheckRadio,
    TumoflipRuntimeDiagnosticCheckBle,
    TumoflipRuntimeDiagnosticCheckDisplay,
    TumoflipRuntimeDiagnosticCheckNfc,
    TumoflipRuntimeDiagnosticCheckGpio,
    TumoflipRuntimeDiagnosticCheckInput,
} TumoflipRuntimeDiagnosticCheckId;

typedef enum {
    TumoflipRuntimeDiagnosticStatusSkipped,
    TumoflipRuntimeDiagnosticStatusPassed,
    TumoflipRuntimeDiagnosticStatusFailed,
} TumoflipRuntimeDiagnosticStatus;

typedef struct {
    TumoflipRuntimeDiagnosticCheckId id;
    TumoflipRuntimeDiagnosticStatus status;
    char detail[TUMOFLIP_RUNTIME_DIAGNOSTIC_DETAIL_MAX + 1U];
} TumoflipRuntimeDiagnosticCheck;

typedef struct {
    uint8_t schema_version;
    uint8_t check_count;
    TumoflipRuntimeDiagnosticCheck checks[TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT];
} TumoflipRuntimeDiagnosticReport;

struct TumoflipRuntimeApi {
    bool (*get_trace)(TumoflipRuntimeApi* api, char* output, size_t output_size);
    bool (*get_fabric_state)(
        TumoflipRuntimeApi* api,
        TumoflipRuntimeFabricSnapshot* snapshot);
    bool (*cancel_fabric)(TumoflipRuntimeApi* api);
    bool (*open_local_fabric)(TumoflipRuntimeApi* api);
    bool (*step_local_fabric)(TumoflipRuntimeApi* api, int8_t delta);
    bool (*get_diagnostics)(
        TumoflipRuntimeApi* api,
        TumoflipRuntimeDiagnosticReport* report);
    void* context;
};

/** Run non-destructive hardware/runtime checks and return a bounded report. */
bool tumoflip_runtime_diagnostics_run(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeDiagnosticReport* report);

/** Format the compact status-only representation used by App Bridge. */
bool tumoflip_runtime_diagnostics_format(
    const TumoflipRuntimeDiagnosticReport* report,
    char* output,
    size_t output_size);
