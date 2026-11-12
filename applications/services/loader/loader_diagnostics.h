#pragma once
#include <furi.h>
#include <flipper_application/flipper_application.h>

typedef enum {
    LoaderDiagnosticNone = 0,
    LoaderDiagnosticOk,
    LoaderDiagnosticBusy,
    LoaderDiagnosticNotFound,
    LoaderDiagnosticStorage,
    LoaderDiagnosticInvalidFile,
    LoaderDiagnosticManifest,
    LoaderDiagnosticImports,
    LoaderDiagnosticApiOld,
    LoaderDiagnosticApiNew,
    LoaderDiagnosticTarget,
    LoaderDiagnosticMemory,
    LoaderDiagnosticFragmented,
    LoaderDiagnosticPlugin,
    LoaderDiagnosticRelocation,
} LoaderDiagnosticCode;

typedef struct {
    uint32_t sequence;
    LoaderDiagnosticCode code;
    uint16_t app_api_major;
    uint16_t app_api_minor;
    uint16_t target;
    uint32_t required;
    uint32_t free_heap;
    uint32_t max_block;
    char app[33];
} LoaderDiagnostic;

void loader_diagnostic_begin(LoaderDiagnostic* diagnostic, const char* name);
void loader_diagnostic_capture(LoaderDiagnostic* diagnostic, FlipperApplication* app);
LoaderDiagnosticCode loader_diagnostic_preload_code(FlipperApplicationPreloadStatus status);
void loader_diagnostic_format(const LoaderDiagnostic* diagnostic, FuriString* output);
