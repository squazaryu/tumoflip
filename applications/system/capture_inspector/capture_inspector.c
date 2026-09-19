#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <string.h>
#include "capture_model.h"
#include "capture_storage.h"

#define CI_STOP       (1U << 0)
#define CI_REPORT_DIR EXT_PATH("apps_data/capture_inspector")

typedef enum {
    CiUiMain,
    CiUiList,
    CiUiText
} CiUiView;
typedef enum {
    CiOpenA,
    CiViewA,
    CiOpenB,
    CiViewB,
    CiCompare,
    CiExport,
    CiAbout
} CiMenu;
typedef enum {
    CiJobLoadA,
    CiJobLoadB,
    CiJobExport
} CiJob;
typedef struct {
    Storage* storage;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    Submenu* fields;
    TextBox* text;
    FuriString* display;
    FuriString* result;
    FuriString* paths[2];
    CiSnapshot captures[2];
    bool ready[2];
    FuriThread* worker;
    CiJob job;
    CiLoadStatus load_status;
    CiUiView current;
    CiUiView text_back;
    unsigned list_kind;
} CiApp;

static void ci_select(void* context, uint32_t action);
static void ci_select_field(void* context, uint32_t index);

static void ci_switch(CiApp* app, CiUiView view) {
    app->current = view;
    view_dispatcher_switch_to_view(app->dispatcher, view);
}

static void ci_show_text(CiApp* app, const char* text, CiUiView back) {
    furi_string_set(app->display, text);
    text_box_reset(app->text);
    text_box_set_text(app->text, furi_string_get_cstr(app->display));
    app->text_back = back;
    ci_switch(app, CiUiText);
}

static void ci_main_menu(CiApp* app) {
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Capture Inspector");
    submenu_add_item(app->menu, "Open file A", CiOpenA, ci_select, app);
    submenu_add_item(
        app->menu, app->ready[0] ? "Inspect A" : "Inspect A (empty)", CiViewA, ci_select, app);
    submenu_add_item(app->menu, "Open file B", CiOpenB, ci_select, app);
    submenu_add_item(
        app->menu, app->ready[1] ? "Inspect B" : "Inspect B (empty)", CiViewB, ci_select, app);
    submenu_add_item(app->menu, "Compare fields", CiCompare, ci_select, app);
    submenu_add_item(app->menu, "Export comparison", CiExport, ci_select, app);
    submenu_add_item(app->menu, "About / limits", CiAbout, ci_select, app);
    ci_switch(app, CiUiMain);
}

static bool ci_cancelled(void* context) {
    UNUSED(context);
    return (furi_thread_flags_get() & CI_STOP) != 0;
}

static bool ci_write_string(File* file, const char* text) {
    const size_t length = strlen(text);
    return storage_file_write(file, text, length) == length;
}

static void ci_export_report(CiApp* app) {
    FuriString* path = furi_string_alloc();
    FuriString* line = furi_string_alloc();
    File* file = storage_file_alloc(app->storage);
    bool created = false;
    bool success = false;
    bool cleanup_failed = false;
    furi_string_set(app->result, "Report was not saved.");
    do {
        if(!storage_simply_mkdir(app->storage, CI_REPORT_DIR)) break;
        for(unsigned i = 0; i < 1000 && !ci_cancelled(NULL); i++) {
            furi_string_printf(path, CI_REPORT_DIR "/compare_%03u.txt", i);
            FS_Error state = storage_common_stat(app->storage, furi_string_get_cstr(path), NULL);
            if(state == FSE_OK) continue;
            if(state != FSE_NOT_EXIST) break;
            created =
                storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
            break;
        }
        if(!created) break;
        if(!ci_write_string(
               file,
               "Capture Inspector report v1\nStored fields only; not RF/CRC verification.\n"
               "Values describe loaded snapshots, not a new read of the files.\n"))
            break;
        furi_string_printf(
            line,
            "A: %s\nB: %s\n\n",
            furi_string_get_cstr(app->paths[0]),
            furi_string_get_cstr(app->paths[1]));
        if(!ci_write_string(file, furi_string_get_cstr(line))) break;
        const size_t count = ci_diff_count(&app->captures[0], &app->captures[1]);
        success = true;
        for(size_t i = 0; i < count; i++) {
            if(ci_cancelled(NULL)) {
                success = false;
                break;
            }
            CiDiffRow row;
            if(!ci_diff_row(&app->captures[0], &app->captures[1], i, &row)) {
                success = false;
                break;
            }
            const char* states[] = {"same", "changed", "only A", "only B"};
            furi_string_printf(
                line,
                "%s #%u [%s]\n A: %s\n B: %s\n",
                row.key,
                (unsigned)row.occurrence + 1,
                states[row.kind],
                row.a ? row.a->value : "<absent>",
                row.b ? row.b->value : "<absent>");
            if(!ci_write_string(file, furi_string_get_cstr(line))) {
                success = false;
                break;
            }
        }
        if(success) success = storage_file_sync(file);
    } while(false);
    const bool closed = storage_file_close(file);
    storage_file_free(file);
    success = success && closed;
    if(created && !success) {
        cleanup_failed = storage_common_remove(app->storage, furi_string_get_cstr(path)) != FSE_OK;
        if(cleanup_failed)
            furi_string_set(
                app->result, "Save failed. SD unavailable.\nAn incomplete report may remain.");
    }
    if(success)
        furi_string_printf(
            app->result,
            "Report saved\n%s\n\nMay contain private capture data.",
            furi_string_get_cstr(path));
    else if(!cleanup_failed && ci_cancelled(NULL))
        furi_string_set(app->result, "Export cancelled.\nOriginal captures unchanged.");
    furi_string_free(line);
    furi_string_free(path);
}

static int32_t ci_worker(void* context) {
    CiApp* app = context;
    if(app->job == CiJobExport) {
        ci_export_report(app);
    } else {
        const unsigned slot = app->job == CiJobLoadA ? 0 : 1;
        app->load_status = ci_capture_load(
            app->storage,
            furi_string_get_cstr(app->paths[slot]),
            &app->captures[slot],
            ci_cancelled,
            NULL);
    }
    return 0;
}

static void ci_begin(CiApp* app, CiJob job) {
    app->job = job;
    if(job != CiJobExport) app->ready[job == CiJobLoadA ? 0 : 1] = false;
    ci_show_text(
        app,
        job == CiJobExport ? "Saving report...\nBack: cancel" : "Reading capture...\nBack: cancel",
        CiUiMain);
    app->worker = furi_thread_alloc_ex("CaptureInspect", 3072, ci_worker, app);
    furi_thread_set_priority(app->worker, FuriThreadPriorityLow);
    furi_thread_start(app->worker);
}

static void ci_list(CiApp* app, unsigned kind) {
    app->list_kind = kind;
    submenu_reset(app->fields);
    submenu_set_header(
        app->fields,
        kind == 2 ? "Compare fields" : (kind ? "File B: stored fields" : "File A: stored fields"));
    if(kind == 2) submenu_add_item(app->fields, "Summary", 1000, ci_select_field, app);
    const size_t count = kind == 2 ? ci_diff_count(&app->captures[0], &app->captures[1]) :
                                     app->captures[kind].count;
    for(size_t i = 0; i < count; i++) {
        char label[56];
        if(kind == 2) {
            CiDiffRow row;
            if(!ci_diff_row(&app->captures[0], &app->captures[1], i, &row)) break;
            const char symbols[] = {'=', '~', '-', '+'};
            snprintf(
                label,
                sizeof(label),
                "[%c] %s #%u",
                symbols[row.kind],
                row.key,
                (unsigned)row.occurrence + 1);
        } else {
            snprintf(
                label, sizeof(label), "%u. %s", (unsigned)i + 1, app->captures[kind].fields[i].key);
        }
        submenu_add_item(app->fields, label, i, ci_select_field, app);
    }
    ci_switch(app, CiUiList);
}

static void ci_select_field(void* context, uint32_t index) {
    CiApp* app = context;
    FuriString* text = furi_string_alloc();
    if(app->list_kind == 2) {
        if(index == 1000) {
            unsigned counts[4] = {0};
            for(size_t i = 0; i < ci_diff_count(&app->captures[0], &app->captures[1]); i++) {
                CiDiffRow row;
                if(ci_diff_row(&app->captures[0], &app->captures[1], i, &row)) counts[row.kind]++;
            }
            furi_string_printf(
                text,
                "Stored field comparison\nSame: %u\nChanged: %u\nOnly A: %u\nOnly B: %u\n\nNo radio or CRC verification.\nField order is ignored; repeated names match by occurrence.",
                counts[0],
                counts[1],
                counts[2],
                counts[3]);
        } else {
            CiDiffRow row;
            if(!ci_diff_row(&app->captures[0], &app->captures[1], index, &row)) {
                furi_string_free(text);
                return;
            }
            furi_string_printf(
                text,
                "%s #%u\n\nA: %s\n\nB: %s\n\nStored values only.",
                row.key,
                (unsigned)row.occurrence + 1,
                row.a ? row.a->value : "<absent>",
                row.b ? row.b->value : "<absent>");
        }
    } else {
        if(index >= app->captures[app->list_kind].count) {
            furi_string_free(text);
            return;
        }
        const CiField* field = &app->captures[app->list_kind].fields[index];
        furi_string_printf(
            text, "%s\n\n%s\n\nStored value; not verified.", field->key, field->value);
    }
    ci_show_text(app, furi_string_get_cstr(text), CiUiList);
    furi_string_free(text);
}

static void ci_tick(void* context) {
    CiApp* app = context;
    if(!app->worker || furi_thread_get_state(app->worker) != FuriThreadStateStopped) return;
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    app->worker = NULL;
    if(app->job == CiJobExport) {
        ci_show_text(app, furi_string_get_cstr(app->result), CiUiMain);
        return;
    }
    const unsigned slot = app->job == CiJobLoadA ? 0 : 1;
    app->ready[slot] = app->load_status == CiLoadOk;
    if(app->ready[slot]) {
        ci_list(app, slot);
        return;
    }
    const char* error = "Storage read error";
    if(app->load_status == CiLoadBadPath)
        error = "Select a .sub or .psf\nfile on the SD card.";
    else if(app->load_status == CiLoadCancelled)
        error = "Reading cancelled";
    else if(app->load_status == CiLoadParseError)
        error = ci_parse_status_text(app->captures[slot].status);
    ci_show_text(app, error, CiUiMain);
}

static bool ci_back(void* context) {
    CiApp* app = context;
    if(app->worker) {
        if(furi_thread_get_state(app->worker) != FuriThreadStateStopped)
            furi_thread_flags_set(furi_thread_get_id(app->worker), CI_STOP);
        ci_show_text(app, "Cancelling...", CiUiMain);
        return true;
    }
    if(app->current == CiUiMain) return false;
    if(app->current == CiUiText && app->text_back == CiUiList)
        ci_switch(app, CiUiList);
    else
        ci_main_menu(app);
    return true;
}

static void ci_select(void* context, uint32_t action) {
    CiApp* app = context;
    if(app->worker) return;
    if(action == CiOpenA || action == CiOpenB) {
        const unsigned slot = action == CiOpenA ? 0 : 1;
        DialogsFileBrowserOptions options;
        dialog_file_browser_set_basic_options(&options, ".sub|.psf", NULL);
        options.base_path = EXT_PATH("");
        options.hide_ext = false;
        FuriString* selected = furi_string_alloc_set(app->paths[slot]);
        if(dialog_file_browser_show(app->dialogs, selected, selected, &options)) {
            furi_string_set(app->paths[slot], selected);
            ci_begin(app, slot ? CiJobLoadB : CiJobLoadA);
        }
        furi_string_free(selected);
    } else if(action == CiViewA || action == CiViewB) {
        const unsigned slot = action == CiViewA ? 0 : 1;
        if(app->ready[slot])
            ci_list(app, slot);
        else
            ci_show_text(app, "Open a capture first.", CiUiMain);
    } else if(action == CiCompare || action == CiExport) {
        if(!app->ready[0] || !app->ready[1])
            ci_show_text(app, "Open files A and B first.", CiUiMain);
        else if(action == CiCompare)
            ci_list(app, 2);
        else
            ci_begin(app, CiJobExport);
    } else if(action == CiAbout) {
        ci_show_text(
            app,
            "Read-only capture metadata.\nNo radio / secret recovery.\n\n"
            "SUB/PSF key files only.\nRAW recordings: use RAW Edit.\n"
            "Limits: 64 KiB, 24 fields,\n31-char names, 159-char values.\n"
            "Unknown fields are kept;\nover-limit files are rejected.\n"
            "CRC values are not verified.\nReports: apps_data/capture_inspector",
            CiUiMain);
    }
}

int32_t capture_inspector_app(void* context) {
    CiApp* app = calloc(1, sizeof(CiApp));
    if(!app) return -1;
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->fields = submenu_alloc();
    app->text = text_box_alloc();
    app->display = furi_string_alloc();
    app->result = furi_string_alloc();
    for(unsigned i = 0; i < 2; i++)
        app->paths[i] = furi_string_alloc_set(EXT_PATH("subghz"));
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, ci_back);
    view_dispatcher_set_tick_event_callback(app->dispatcher, ci_tick, 100);
    view_dispatcher_attach_to_gui(
        app->dispatcher, furi_record_open(RECORD_GUI), ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->dispatcher, CiUiMain, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, CiUiList, submenu_get_view(app->fields));
    view_dispatcher_add_view(app->dispatcher, CiUiText, text_box_get_view(app->text));
    text_box_set_font(app->text, TextBoxFontText);
    ci_main_menu(app);
    if(context && ci_capture_path_valid(context)) {
        furi_string_set(app->paths[0], (const char*)context);
        ci_begin(app, CiJobLoadA);
    }
    view_dispatcher_run(app->dispatcher);
    if(app->worker) {
        if(furi_thread_get_state(app->worker) != FuriThreadStateStopped)
            furi_thread_flags_set(furi_thread_get_id(app->worker), CI_STOP);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    view_dispatcher_remove_view(app->dispatcher, CiUiText);
    view_dispatcher_remove_view(app->dispatcher, CiUiList);
    view_dispatcher_remove_view(app->dispatcher, CiUiMain);
    text_box_free(app->text);
    submenu_free(app->fields);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    for(unsigned i = 0; i < 2; i++)
        furi_string_free(app->paths[i]);
    furi_string_free(app->result);
    furi_string_free(app->display);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
