#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <tumoflip_runtime/tumoflip_runtime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNTIME_TRACE_VIEWER_DATA_DIR EXT_PATH("apps_data/runtime_trace_viewer")
#define RUNTIME_TRACE_VIEWER_EXPORT_PATH_SIZE 128U
#define RUNTIME_TRACE_VIEWER_TRACE_MAX 160U

typedef enum {
    RuntimeTraceViewerViewMenu,
    RuntimeTraceViewerViewText,
} RuntimeTraceViewerView;

typedef enum {
    RuntimeTraceViewerActionDiagnostics,
    RuntimeTraceViewerActionRefresh,
    RuntimeTraceViewerActionExport,
    RuntimeTraceViewerActionAbout,
} RuntimeTraceViewerAction;

typedef struct {
    Gui* gui;
    Storage* storage;
    TumoflipRuntimeApi* runtime;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    FuriString* text;
} RuntimeTraceViewerApp;

static bool runtime_trace_viewer_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static void runtime_trace_viewer_format_filename_timestamp(char* output, size_t output_size) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        output,
        output_size,
        "%04u%02u%02u_%02u%02u%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static void runtime_trace_viewer_append_iso_timestamp(FuriString* output) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    furi_string_cat_printf(
        output,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static const char* runtime_trace_viewer_event_code(char code) {
    switch(code) {
    case 'r':
        return "request";
    case 't':
        return "reply";
    case 'e':
        return "error";
    case 's':
        return "session";
    default:
        return "event";
    }
}

static const char* runtime_trace_viewer_command_name(char command) {
    switch(command) {
    case 'c':
        return "capabilities";
    case 'h':
        return "hello";
    case 'p':
        return "ping/pong";
    case 's':
        return "status";
    case 't':
        return "trace/twin";
    case 'e':
        return "error";
    default:
        return "unknown";
    }
}

static bool runtime_trace_viewer_get_trace(RuntimeTraceViewerApp* app, char* output, size_t size) {
    if(!app->runtime || !app->runtime->get_trace) {
        snprintf(output, size, "schema=1;depth=0;count=0");
        return false;
    }

    return app->runtime->get_trace(app->runtime, output, size);
}

static const char* runtime_trace_viewer_check_name(TumoflipRuntimeDiagnosticCheckId id) {
    static const char* const names[TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT] = {
        "Identity",
        "Heap",
        "Storage",
        "Battery",
        "Radio",
        "BLE",
        "Display",
        "NFC",
        "GPIO",
        "Input",
    };

    return id < TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT ? names[id] : "Unknown";
}

static const char* runtime_trace_viewer_check_status(TumoflipRuntimeDiagnosticStatus status) {
    switch(status) {
    case TumoflipRuntimeDiagnosticStatusPassed:
        return "PASS";
    case TumoflipRuntimeDiagnosticStatusFailed:
        return "FAIL";
    case TumoflipRuntimeDiagnosticStatusSkipped:
    default:
        return "SKIP";
    }
}

static void runtime_trace_viewer_build_diagnostics(RuntimeTraceViewerApp* app) {
    TumoflipRuntimeDiagnosticReport report;
    const bool available = app->runtime && app->runtime->get_diagnostics &&
                           app->runtime->get_diagnostics(app->runtime, &report);

    furi_string_reset(app->text);
    furi_string_cat(app->text, "Tumo Diagnostics\n\n");
    if(!available) {
        furi_string_cat(app->text, "Runtime diagnostics unavailable.\n");
        return;
    }

    furi_string_cat_printf(
        app->text, "Schema: %u\nChecks: %u\n\n", report.schema_version, report.check_count);
    for(uint8_t index = 0U;
        index < report.check_count && index < TUMOFLIP_RUNTIME_DIAGNOSTIC_CHECK_COUNT;
        index++) {
        const TumoflipRuntimeDiagnosticCheck* check = &report.checks[index];
        furi_string_cat_printf(
            app->text,
            "%s  %s\n%s\n\n",
            runtime_trace_viewer_check_status(check->status),
            runtime_trace_viewer_check_name(check->id),
            check->detail);
    }
}

static void runtime_trace_viewer_append_event(FuriString* output, const char* event, uint8_t index) {
    char code = '?';
    char command = '?';
    char result = '?';
    if(sscanf(event, "%c,%c,%c", &code, &command, &result) != 3) {
        furi_string_cat_printf(output, "%u. malformed: %s\n", index, event);
        return;
    }

    furi_string_cat_printf(
        output,
        "%u. %s %s %s\n",
        index,
        runtime_trace_viewer_event_code(code),
        runtime_trace_viewer_command_name(command),
        result == 'e' ? "ERR" : "OK");
}

static void runtime_trace_viewer_build_report(RuntimeTraceViewerApp* app, FuriString* output) {
    char trace[RUNTIME_TRACE_VIEWER_TRACE_MAX];
    const bool live = runtime_trace_viewer_get_trace(app, trace, sizeof(trace));

    furi_string_reset(output);
    furi_string_cat(output, "Runtime Trace\n");
    furi_string_cat(output, "Generated: ");
    runtime_trace_viewer_append_iso_timestamp(output);
    furi_string_cat_printf(output, "\nSource: %s\n\n", live ? "runtime service" : "unavailable");
    furi_string_cat_printf(output, "Raw\n%s\n\nEvents\n", trace);

    char* event = strchr(trace, '|');
    uint8_t index = 1U;
    while(event) {
        event++;
        char* next = strchr(event, '|');
        if(next) {
            *next = '\0';
        }
        if(event[0]) {
            runtime_trace_viewer_append_event(output, event, index++);
        }
        event = next;
    }

    if(index == 1U) {
        furi_string_cat(output, "No events yet.\n");
    }
}

static bool runtime_trace_viewer_write_text_file(
    Storage* storage,
    const char* path,
    const char* text) {
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        const size_t size = strlen(text);
        ok = storage_file_write(file, text, size) == size;
        storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static void runtime_trace_viewer_export_report(RuntimeTraceViewerApp* app) {
    char timestamp[32];
    char path[RUNTIME_TRACE_VIEWER_EXPORT_PATH_SIZE];
    runtime_trace_viewer_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(path, sizeof(path), RUNTIME_TRACE_VIEWER_DATA_DIR "/trace_%s.txt", timestamp);

    FuriString* report = furi_string_alloc();
    runtime_trace_viewer_build_report(app, report);

    const bool dir_ok = runtime_trace_viewer_mkdir(app->storage, RUNTIME_TRACE_VIEWER_DATA_DIR);
    const bool write_ok = dir_ok && runtime_trace_viewer_write_text_file(
                                        app->storage, path, furi_string_get_cstr(report));

    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Runtime Trace Export\n\n"
        "Result: %s\n"
        "Path:\n%s\n\n",
        write_ok ? "saved" : (dir_ok ? "write failed" : "mkdir failed"),
        path);
    furi_string_cat(app->text, report);
    furi_string_free(report);
}

static void runtime_trace_viewer_build_about(RuntimeTraceViewerApp* app) {
    furi_string_reset(app->text);
    furi_string_cat(
        app->text,
        "Tumo Diagnostics\n\n"
        "Runs bounded device checks, shows the compact Tumoflip runtime ring buffer, and exports a text report to SD.\n\n"
        "The trace stores command metadata only: event type, command family, and result. It does not store payloads.");
}

static void runtime_trace_viewer_show_text(RuntimeTraceViewerApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, RuntimeTraceViewerViewText);
}

static void runtime_trace_viewer_menu_callback(void* context, uint32_t index) {
    RuntimeTraceViewerApp* app = context;

    switch(index) {
    case RuntimeTraceViewerActionDiagnostics:
        runtime_trace_viewer_build_diagnostics(app);
        break;
    case RuntimeTraceViewerActionRefresh:
        runtime_trace_viewer_build_report(app, app->text);
        break;
    case RuntimeTraceViewerActionExport:
        runtime_trace_viewer_export_report(app);
        break;
    case RuntimeTraceViewerActionAbout:
        runtime_trace_viewer_build_about(app);
        break;
    default:
        furi_string_reset(app->text);
        furi_string_cat(app->text, "Unsupported action.\n");
        break;
    }

    runtime_trace_viewer_show_text(app);
}

static bool runtime_trace_viewer_back_callback(void* context) {
    view_dispatcher_stop(((RuntimeTraceViewerApp*)context)->view_dispatcher);
    return true;
}

static uint32_t runtime_trace_viewer_text_previous_callback(void* context) {
    UNUSED(context);
    return RuntimeTraceViewerViewMenu;
}

static RuntimeTraceViewerApp* runtime_trace_viewer_alloc(void) {
    RuntimeTraceViewerApp* app = malloc(sizeof(RuntimeTraceViewerApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->runtime = furi_record_open(RECORD_TUMOFLIP_RUNTIME);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_box = text_box_alloc();
    app->text = furi_string_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, runtime_trace_viewer_back_callback);

    submenu_set_header(app->submenu, "Tumo Diagnostics");
    submenu_add_item(
        app->submenu,
        "Hardware Diagnostics",
        RuntimeTraceViewerActionDiagnostics,
        runtime_trace_viewer_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Refresh",
        RuntimeTraceViewerActionRefresh,
        runtime_trace_viewer_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Export Report",
        RuntimeTraceViewerActionExport,
        runtime_trace_viewer_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "About",
        RuntimeTraceViewerActionAbout,
        runtime_trace_viewer_menu_callback,
        app);

    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(
        text_box_get_view(app->text_box), runtime_trace_viewer_text_previous_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, RuntimeTraceViewerViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, RuntimeTraceViewerViewText, text_box_get_view(app->text_box));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, RuntimeTraceViewerViewMenu);
    return app;
}

static void runtime_trace_viewer_free(RuntimeTraceViewerApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, RuntimeTraceViewerViewText);
    view_dispatcher_remove_view(app->view_dispatcher, RuntimeTraceViewerViewMenu);
    text_box_free(app->text_box);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->text);
    furi_record_close(RECORD_TUMOFLIP_RUNTIME);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t runtime_trace_viewer_app(void* context) {
    UNUSED(context);
    RuntimeTraceViewerApp* app = runtime_trace_viewer_alloc();
    view_dispatcher_run(app->view_dispatcher);
    runtime_trace_viewer_free(app);
    return 0;
}
