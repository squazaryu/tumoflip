#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TUMOFLIP_ROADMAP_FULL
#define TUMOFLIP_ROADMAP_FULL 0
#endif

/*
 * The compact release profile keeps the public diagnostic contract but omits
 * optional manifest and heap-detail collection.  Engineering builds can opt
 * into the complete payload with --extra-define=TUMOFLIP_ROADMAP_FULL=1.
 */
#define TUMOFLIP_LOADER_DIAGNOSTICS_FULL TUMOFLIP_ROADMAP_FULL

/** Version of the machine-readable loader diagnostic contract. */
#define LOADER_DIAGNOSTIC_SCHEMA_VERSION 1U
#define LOADER_DIAGNOSTIC_APP_ID_MAX      48U

typedef enum {
    LoaderDiagnosticCodeNone,
    LoaderDiagnosticCodeSuccess,
    LoaderDiagnosticCodeBusy,
    LoaderDiagnosticCodeNotFound,
    LoaderDiagnosticCodeInvalidFile,
    LoaderDiagnosticCodeInvalidManifest,
    LoaderDiagnosticCodeMissingImports,
    LoaderDiagnosticCodeApiTooOld,
    LoaderDiagnosticCodeApiTooNew,
    LoaderDiagnosticCodeTargetMismatch,
    LoaderDiagnosticCodeInsufficientMemory,
    LoaderDiagnosticCodeInsufficientContiguousMemory,
    LoaderDiagnosticCodePluginNotRunnable,
    LoaderDiagnosticCodeStorageUnavailable,
    LoaderDiagnosticCodeInternal,
} LoaderDiagnosticCode;

typedef enum {
    LoaderDiagnosticActionNone,
    LoaderDiagnosticActionLaunch,
    LoaderDiagnosticActionCloseRunningApp,
    LoaderDiagnosticActionInstallApp,
    LoaderDiagnosticActionUpdateApp,
    LoaderDiagnosticActionUpdateFirmware,
    LoaderDiagnosticActionFreeMemory,
    LoaderDiagnosticActionInsertSd,
    LoaderDiagnosticActionInspectLogs,
} LoaderDiagnosticAction;

/**
 * A bounded, pointer-free description of an application launch result.
 *
 * The structure is intentionally suitable for the Loader service boundary:
 * callers may retain it after the synchronous launch request returns, and no
 * string owned by the loader is exposed.
 */
typedef struct {
    uint8_t schema_version;
    LoaderDiagnosticCode code;
    LoaderDiagnosticAction action;
    uint16_t required_api_major;
    uint16_t required_api_minor;
    uint16_t firmware_api_major;
    uint16_t firmware_api_minor;
    uint16_t required_target;
    uint16_t firmware_target;
    uint32_t free_heap;
    uint32_t max_free_block;
    char app_id[LOADER_DIAGNOSTIC_APP_ID_MAX + 1U];
} LoaderDiagnostic;

void loader_diagnostic_reset(LoaderDiagnostic* diagnostic, const char* app_id);
const char* loader_diagnostic_code_to_string(LoaderDiagnosticCode code);
const char* loader_diagnostic_action_to_string(LoaderDiagnosticAction action);

/** Format a stable key/value payload for CLI, App Bridge and companion apps. */
bool loader_diagnostic_format(
    const LoaderDiagnostic* diagnostic,
    char* output,
    size_t output_size);

#ifdef __cplusplus
}
#endif
