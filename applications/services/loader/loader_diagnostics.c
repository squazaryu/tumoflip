#include "loader_diagnostics.h"
#include <loader/firmware_api/firmware_api.h>
#include <string.h>

void loader_diagnostic_begin(LoaderDiagnostic* d, const char* name) {
    uint32_t sequence = d->sequence + 1;
    memset(d, 0, sizeof(*d));
    d->sequence = sequence;
    d->code = LoaderDiagnosticOk;
    const char* base = name ? strrchr(name, '/') : NULL;
    name = base ? base + 1 : name;
    for(size_t i = 0; name && name[i] && i < sizeof(d->app) - 1; i++) {
        const unsigned char c = name[i];
        d->app[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.') ? c : '_';
    }
    d->free_heap = memmgr_get_free_heap();
    d->max_block = memmgr_heap_get_max_free_block();
}

LoaderDiagnosticCode loader_diagnostic_preload_code(FlipperApplicationPreloadStatus status) {
    switch(status) {
    case FlipperApplicationPreloadStatusSuccess: return LoaderDiagnosticOk;
    case FlipperApplicationPreloadStatusInvalidFile: return LoaderDiagnosticInvalidFile;
    case FlipperApplicationPreloadStatusInvalidManifest: return LoaderDiagnosticManifest;
    case FlipperApplicationPreloadStatusNotEnoughMemory: return LoaderDiagnosticMemory;
    case FlipperApplicationPreloadStatusApiTooOld: return LoaderDiagnosticApiOld;
    case FlipperApplicationPreloadStatusApiTooNew: return LoaderDiagnosticApiNew;
    case FlipperApplicationPreloadStatusTargetMismatch: return LoaderDiagnosticTarget;
    default: return LoaderDiagnosticInvalidFile;
    }
}

void loader_diagnostic_capture(LoaderDiagnostic* d, FlipperApplication* app) {
    const FlipperApplicationManifest* m = flipper_application_get_manifest(app);
    if(m->base.manifest_magic == FAP_MANIFEST_MAGIC &&
       m->base.manifest_version == FAP_MANIFEST_SUPPORTED_VERSION) {
        d->app_api_major = m->base.api_version.major;
        d->app_api_minor = m->base.api_version.minor;
        d->target = m->base.hardware_target_id;
    }
    size_t required, free_heap, max_block;
    if(flipper_application_get_memory_failure(app, &required, &free_heap, &max_block)) {
        d->required = required;
        d->free_heap = free_heap;
        d->max_block = max_block;
        d->code = free_heap >= required && max_block < required ?
                      LoaderDiagnosticFragmented : LoaderDiagnosticMemory;
    }
}

void loader_diagnostic_format(const LoaderDiagnostic* d, FuriString* output) {
    static const char* const codes[] = {
        "none", "ok", "busy", "not_found", "storage_unavailable", "invalid_file",
        "invalid_manifest", "missing_imports", "api_too_old", "api_too_new",
        "target_mismatch", "not_enough_memory", "fragmented_memory",
        "plugin_not_runnable", "relocation_failed",
    };
    static const char* const actions[] = {
        "none", "none", "close_app", "install_app", "check_sd", "update_app",
        "update_app", "update_package_or_firmware", "update_app", "update_firmware",
        "use_f7_app", "free_memory", "reboot", "open_host_app", "update_app",
    };
    unsigned code = (unsigned)d->code < COUNT_OF(codes) ? d->code : LoaderDiagnosticInvalidFile;
    furi_string_printf(output,
        "schema=1;seq=%lu;code=%s;app=%s;app_api=%u.%u;fw_api=%u.%u;"
        "target=%u;required=%lu;free=%lu;largest=%lu;action=%s",
        (unsigned long)d->sequence, codes[code], d->app,
        d->app_api_major, d->app_api_minor,
        firmware_api_interface->api_version_major, firmware_api_interface->api_version_minor,
        d->target, (unsigned long)d->required, (unsigned long)d->free_heap,
        (unsigned long)d->max_block, actions[code]);
}
