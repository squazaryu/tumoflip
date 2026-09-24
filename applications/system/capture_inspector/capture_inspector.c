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
#define CI_LIST_COMPARE_AB 2U
#define CI_LIST_COMPARE_SERIES 3U
#define CI_LIST_FILE_C 4U

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
    CiAbout,
    CiOpenC,
    CiViewC,
    CiCompareSeries,
} CiMenu;
typedef enum {
    CiJobLoadA,
    CiJobLoadB,
    CiJobLoadC,
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
    FuriString* paths[CI_SERIES_MAX];
    CiSnapshot captures[CI_SERIES_MAX];
    bool ready[CI_SERIES_MAX];
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
    submenu_add_item(app->menu, "Open file C", CiOpenC, ci_select, app);
    submenu_add_item(
        app->menu, app->ready[2] ? "Inspect C" : "Inspect C (empty)", CiViewC, ci_select, app);
    submenu_add_item(app->menu, "Compare fields", CiCompare, ci_select, app);
    submenu_add_item(app->menu, "Compare series", CiCompareSeries, ci_select, app);
    submenu_add_item(app->menu, "Export comparison", CiExport, ci_select, app);
    submenu_add_item(app->menu, "About / limits", CiAbout, ci_select, app);
    ci_switch(app, CiUiMain);
}

static bool ci_series_paths_are_distinct(const CiApp* app, size_t sample_count) {
    const char* paths[CI_SERIES_MAX] = {0};
    if(sample_count > CI_SERIES_MAX) return false;
    for(size_t sample = 0U; sample < sample_count; sample++) {
        paths[sample] = furi_string_get_cstr(app->paths[sample]);
    }
    return ci_capture_paths_are_distinct(paths, sample_count);
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
        const size_t sample_count = app->ready[2] ? CI_SERIES_MAX : 2U;
        if(!ci_series_paths_are_distinct(app, sample_count)) {
            furi_string_set(app->result, "Choose different files for the series report.");
            break;
        }
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
               "Capture Inspector series report v2\nStored fields only; not RF/CRC verification.\n"
               "Values describe loaded snapshots, not a new read of the files.\n"))
            break;
        furi_string_printf(
            line,
            "A: %s\nB: %s\n",
            furi_string_get_cstr(app->paths[0]),
            furi_string_get_cstr(app->paths[1]));
        if(sample_count == CI_SERIES_MAX)
            furi_string_cat_printf(line, "C: %s\n", furi_string_get_cstr(app->paths[2]));
        furi_string_cat(line, "\n");
        if(!ci_write_string(file, furi_string_get_cstr(line))) break;
        const size_t count = ci_series_diff_count(app->captures, sample_count);
        success = true;
        for(size_t i = 0; i < count; i++) {
            if(ci_cancelled(NULL)) {
                success = false;
                break;
            }
            CiSeriesRow row;
            if(!ci_series_diff_row(app->captures, sample_count, i, &row)) {
                success = false;
                break;
            }
            furi_string_printf(
                line,
                "%s #%u [%s]",
                row.key,
                (unsigned)row.occurrence + 1,
                row.changed ? "changed / missing" : "stable");
            static const char* const names[CI_SERIES_MAX] = {"A", "B", "C"};
            for(size_t sample = 0U; sample < sample_count; sample++) {
                furi_string_cat_printf(
                    line,
                    "\n %s: %s",
                    names[sample],
                    row.samples[sample] ? row.samples[sample]->value : "<absent>");
            }
            furi_string_cat(line, "\n");
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
        const unsigned slot = app->job == CiJobLoadA ? 0U : app->job == CiJobLoadB ? 1U : 2U;
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
    if(job != CiJobExport) {
        const unsigned slot = job == CiJobLoadA ? 0U : job == CiJobLoadB ? 1U : 2U;
        app->ready[slot] = false;
    }
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
        kind == CI_LIST_COMPARE_SERIES ? "Compare series" :
        kind == CI_LIST_COMPARE_AB     ? "Compare A / B" :
        kind ? (kind == 1 ? "File B: stored fields" : "File C: stored fields") :
               "File A: stored fields");
    if(kind == CI_LIST_COMPARE_AB || kind == CI_LIST_COMPARE_SERIES)
        submenu_add_item(app->fields, "Summary", 1000, ci_select_field, app);
    const size_t sample_count = app->ready[2] ? CI_SERIES_MAX : 2U;
    const unsigned file_slot = kind == CI_LIST_FILE_C ? 2U : kind;
    const size_t count =
        kind == CI_LIST_COMPARE_AB     ? ci_diff_count(&app->captures[0], &app->captures[1]) :
        kind == CI_LIST_COMPARE_SERIES ? ci_series_diff_count(app->captures, sample_count) :
                                         app->captures[file_slot].count;
    for(size_t i = 0; i < count; i++) {
        char label[56];
        if(kind == CI_LIST_COMPARE_AB) {
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
        } else if(kind == CI_LIST_COMPARE_SERIES) {
            CiSeriesRow row;
            if(!ci_series_diff_row(app->captures, sample_count, i, &row)) break;
            snprintf(
                label,
                sizeof(label),
                "[%c] %s #%u",
                row.changed ? '~' : '=',
                row.key,
                (unsigned)row.occurrence + 1U);
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
    if(app->list_kind == CI_LIST_COMPARE_AB) {
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
    } else if(app->list_kind == CI_LIST_COMPARE_SERIES) {
        const size_t sample_count = app->ready[2] ? CI_SERIES_MAX : 2U;
        if(index == 1000U) {
            unsigned stable = 0U;
            unsigned changed = 0U;
            const size_t count = ci_series_diff_count(app->captures, sample_count);
            for(size_t row_index = 0U; row_index < count; row_index++) {
                CiSeriesRow row;
                if(!ci_series_diff_row(app->captures, sample_count, row_index, &row)) continue;
                if(row.changed)
                    changed++;
                else
                    stable++;
            }
            furi_string_printf(
                text,
                "Stored-field series\nSamples: %u\nFields: %u\nStable: %u\nChanged / missing: %u\n\n"
                "Values are read from files.\nNo RF or CRC verification.",
                (unsigned)sample_count,
                (unsigned)count,
                stable,
                changed);
        } else {
            CiSeriesRow row;
            if(!ci_series_diff_row(app->captures, sample_count, index, &row)) {
                furi_string_free(text);
                return;
            }
            furi_string_printf(
                text,
                "%s #%u\n%s across %u samples",
                row.key,
                (unsigned)row.occurrence + 1U,
                row.changed ? "Changed or missing" : "Stable",
                (unsigned)sample_count);
            static const char* const names[CI_SERIES_MAX] = {"A", "B", "C"};
            for(size_t sample = 0U; sample < sample_count; sample++) {
                furi_string_cat_printf(
                    text,
                    "\n\n%s: %s",
                    names[sample],
                    row.samples[sample] ? row.samples[sample]->value : "<absent>");
            }
            furi_string_cat(text, "\n\nStored values only.");
        }
    } else {
        const unsigned file_slot = app->list_kind == CI_LIST_FILE_C ? 2U : app->list_kind;
        if(index >= app->captures[file_slot].count) {
            furi_string_free(text);
            return;
        }
        const CiField* field = &app->captures[file_slot].fields[index];
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
    const unsigned slot = app->job == CiJobLoadA ? 0U : app->job == CiJobLoadB ? 1U : 2U;
    app->ready[slot] = app->load_status == CiLoadOk;
    if(app->ready[slot]) {
        ci_list(app, slot == 2U ? CI_LIST_FILE_C : slot);
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
    if(action == CiOpenA || action == CiOpenB || action == CiOpenC) {
        const unsigned slot = action == CiOpenA ? 0U : action == CiOpenB ? 1U : 2U;
        DialogsFileBrowserOptions options;
        dialog_file_browser_set_basic_options(&options, ".sub|.psf", NULL);
        options.base_path = EXT_PATH("");
        options.hide_ext = false;
        FuriString* selected = furi_string_alloc_set(app->paths[slot]);
        if(dialog_file_browser_show(app->dialogs, selected, selected, &options)) {
            furi_string_set(app->paths[slot], selected);
            ci_begin(app, slot == 0U ? CiJobLoadA : slot == 1U ? CiJobLoadB : CiJobLoadC);
        }
        furi_string_free(selected);
    } else if(action == CiViewA || action == CiViewB || action == CiViewC) {
        const unsigned slot = action == CiViewA ? 0U : action == CiViewB ? 1U : 2U;
        if(app->ready[slot])
            ci_list(app, slot == 2U ? CI_LIST_FILE_C : slot);
        else
            ci_show_text(app, "Open a capture first.", CiUiMain);
    } else if(action == CiCompare || action == CiExport) {
        if(!app->ready[0] || !app->ready[1])
            ci_show_text(app, "Open files A and B first.", CiUiMain);
        else if(action == CiCompare)
            ci_list(app, CI_LIST_COMPARE_AB);
        else
            ci_begin(app, CiJobExport);
    } else if(action == CiCompareSeries) {
        if(!app->ready[0] || !app->ready[1])
            ci_show_text(app, "Open at least files A and B first.", CiUiMain);
        else if(!ci_series_paths_are_distinct(app, app->ready[2] ? CI_SERIES_MAX : 2U))
            ci_show_text(app, "Open different files for each sample.", CiUiMain);
        else
            ci_list(app, CI_LIST_COMPARE_SERIES);
    } else if(action == CiAbout) {
        ci_show_text(
            app,
            "Read-only capture metadata.\nNo radio / secret recovery.\n\n"
            "SUB/PSF key files only.\nRAW timings: use TumoSpectrum.\n"
            "Compare two or three captures.\nLimits per file: 64 KiB, 24 fields,\n"
            "31-char names, 159-char values.\n"
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
    for(unsigned i = 0; i < CI_SERIES_MAX; i++)
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
    for(unsigned i = 0; i < CI_SERIES_MAX; i++)
        furi_string_free(app->paths[i]);
    furi_string_free(app->result);
    furi_string_free(app->display);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
