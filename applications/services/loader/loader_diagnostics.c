#include "loader_diagnostics.h"

#include <stdio.h>
#include <string.h>

void loader_diagnostic_reset(LoaderDiagnostic* diagnostic, const char* app_id) {
    if(!diagnostic) return;

    memset(diagnostic, 0, sizeof(LoaderDiagnostic));
    diagnostic->schema_version = LOADER_DIAGNOSTIC_SCHEMA_VERSION;
    if(app_id) strlcpy(diagnostic->app_id, app_id, sizeof(diagnostic->app_id));
}

const char* loader_diagnostic_code_to_string(LoaderDiagnosticCode code) {
#if !TUMOFLIP_LOADER_DIAGNOSTICS_FULL
    (void)code;
    return "compact";
#else
    switch(code) {
    case LoaderDiagnosticCodeSuccess:
        return "success";
    case LoaderDiagnosticCodeBusy:
        return "busy";
    case LoaderDiagnosticCodeNotFound:
        return "not_found";
    case LoaderDiagnosticCodeInvalidFile:
        return "invalid_file";
    case LoaderDiagnosticCodeInvalidManifest:
        return "invalid_manifest";
    case LoaderDiagnosticCodeMissingImports:
        return "missing_imports";
    case LoaderDiagnosticCodeApiTooOld:
        return "api_too_old";
    case LoaderDiagnosticCodeApiTooNew:
        return "api_too_new";
    case LoaderDiagnosticCodeTargetMismatch:
        return "target_mismatch";
    case LoaderDiagnosticCodeInsufficientMemory:
        return "insufficient_memory";
    case LoaderDiagnosticCodeInsufficientContiguousMemory:
        return "insufficient_contiguous_memory";
    case LoaderDiagnosticCodePluginNotRunnable:
        return "plugin_not_runnable";
    case LoaderDiagnosticCodeStorageUnavailable:
        return "storage_unavailable";
    case LoaderDiagnosticCodeInternal:
        return "internal";
    case LoaderDiagnosticCodeNone:
    default:
        return "none";
    }
#endif
}

const char* loader_diagnostic_action_to_string(LoaderDiagnosticAction action) {
#if !TUMOFLIP_LOADER_DIAGNOSTICS_FULL
    (void)action;
    return "compact";
#else
    switch(action) {
    case LoaderDiagnosticActionLaunch:
        return "launch";
    case LoaderDiagnosticActionCloseRunningApp:
        return "close_running_app";
    case LoaderDiagnosticActionInstallApp:
        return "install_app";
    case LoaderDiagnosticActionUpdateApp:
        return "update_app";
    case LoaderDiagnosticActionUpdateFirmware:
        return "update_firmware";
    case LoaderDiagnosticActionFreeMemory:
        return "free_memory";
    case LoaderDiagnosticActionInsertSd:
        return "insert_sd";
    case LoaderDiagnosticActionInspectLogs:
        return "inspect_logs";
    case LoaderDiagnosticActionNone:
    default:
        return "none";
    }
#endif
}

bool loader_diagnostic_format(
    const LoaderDiagnostic* diagnostic,
    char* output,
    size_t output_size) {
    if(!diagnostic || !output || output_size == 0U) return false;

#if TUMOFLIP_LOADER_DIAGNOSTICS_FULL
    const int written = snprintf(
        output,
        output_size,
        "schema=%u;code=%s;action=%s;app=%s;required_api=%u.%u;firmware_api=%u.%u;"
        "required_target=%u;firmware_target=%u;free_heap=%lu;max_free_block=%lu",
        diagnostic->schema_version,
        loader_diagnostic_code_to_string(diagnostic->code),
        loader_diagnostic_action_to_string(diagnostic->action),
        diagnostic->app_id[0] ? diagnostic->app_id : "none",
        diagnostic->required_api_major,
        diagnostic->required_api_minor,
        diagnostic->firmware_api_major,
        diagnostic->firmware_api_minor,
        diagnostic->required_target,
        diagnostic->firmware_target,
        (unsigned long)diagnostic->free_heap,
        (unsigned long)diagnostic->max_free_block);
#else
    /* Keep the error code/action usable by CLI, RPC and companion apps while
     * omitting optional heap/manifest fields from the compact image. */
    const int written = snprintf(
        output,
        output_size,
        "schema=%u;code=%u;action=%u;app=%s",
        diagnostic->schema_version,
        diagnostic->code,
        diagnostic->action,
        diagnostic->app_id[0] ? diagnostic->app_id : "none");
#endif

    return written >= 0 && (size_t)written < output_size;
}
