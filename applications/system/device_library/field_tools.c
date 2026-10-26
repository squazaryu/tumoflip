#include "field_menu.h"
#include "field_bundle.h"
#include "field_inventory.h"
#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/view_dispatcher.h>
#include <dialogs/dialogs.h>
#include <loader/loader.h>
#include <stdio.h>
#include <string.h>

enum {
    FieldMenuView,
    FieldTextView,
    FieldInputView,
    FieldJobDone = 0xF001,
    FieldNameDone
};
typedef enum {
    FieldJobExport,
    FieldJobInspect,
    FieldJobImport,
    FieldJobSnapshot,
    FieldJobCompare
} FieldJob;
typedef struct {
    Storage* storage;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextBox* text;
    TextInput* input;
    FuriString* message;
    FuriString* selected_path;
    FieldMenuState state;
    FieldPage back;
    FieldRound round;
    DeviceCard card;
    char names[32][32];
    char edit[64], path[256], report_path[256];
    FieldJob job;
    bool job_ok, report_ok;
    FuriThread* thread;
    File* report;
    FieldInventoryStats stats;
} FieldTools;

static void field_tools_show(FieldTools* app, FieldPage page);
static void field_tools_name_done(void* context);
static void field_tools_action(void* context, uint32_t action) {
    view_dispatcher_send_custom_event(((FieldTools*)context)->dispatcher, action);
}
static void field_tools_message(FieldTools* app, const char* message, FieldPage back) {
    if(message != furi_string_get_cstr(app->message)) furi_string_set(app->message, message);
    text_box_reset(app->text);
    text_box_set_text(app->text, furi_string_get_cstr(app->message));
    app->back = back;
    app->state.page = FieldPageMessage;
    view_dispatcher_switch_to_view(app->dispatcher, FieldTextView);
}
static void field_tools_cards(FieldTools* app) {
    app->state.count = 0;
    app->state.more = false;
    uint32_t seen = 0;
    File* dir = storage_file_alloc(app->storage);
    if(storage_dir_open(dir, LIBRARY_CARDS)) {
        FileInfo info;
        char name[64];
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(!file_info_is_dir(&info) || !library_name_valid(name)) continue;
            if(seen++ < app->state.offset) continue;
            if(app->state.count == 32) {
                app->state.more = true;
                break;
            }
            memcpy(app->names[app->state.count++], name, strlen(name) + 1);
        }
    }
    storage_file_free(dir);
}
static void field_tools_show(FieldTools* app, FieldPage page) {
    app->state.page = page;
    if(page == FieldPageCards) field_tools_cards(app);
    field_menu_build(app->menu, &app->state, field_tools_action, app);
    view_dispatcher_switch_to_view(app->dispatcher, FieldMenuView);
}
static bool field_tools_choose(FieldTools* app, const char* root, const char* extension) {
    furi_string_set(app->selected_path, root);
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, extension, NULL);
    options.base_path = root;
    if(!dialog_file_browser_show(app->dialogs, app->selected_path, app->selected_path, &options))
        return false;
    const char* path = furi_string_get_cstr(app->selected_path);
    if(!library_path_valid(path)) return false;
    memcpy(app->path, path, strlen(path) + 1);
    return true;
}
static bool field_tools_new_path(
    FieldTools* app,
    const char* folder,
    const char* extension,
    char path[256]) {
    char directory[128];
    int n = snprintf(directory, sizeof(directory), LIBRARY_ROOT "/%s", folder);
    if(n < 0 || (size_t)n >= sizeof(directory) ||
       !storage_simply_mkdir(app->storage, LIBRARY_ROOT) ||
       !storage_simply_mkdir(app->storage, directory))
        return false;
    uint32_t now = furi_hal_rtc_get_timestamp();
    for(unsigned i = 0; i < 100; i++) {
        n = snprintf(path, 256, "%s/%lu-%02u%s", directory, (unsigned long)now, i, extension);
        if(n < 0 || n >= 256) return false;
        FS_Error status = storage_common_stat(app->storage, path, NULL);
        if(status == FSE_NOT_EXIST) return true;
        if(status != FSE_OK) return false;
    }
    return false;
}
static bool field_tools_progress(void* context, const char* path, uint32_t completed) {
    UNUSED(context);
    UNUSED(path);
    UNUSED(completed);
    return !(furi_thread_flags_get() & 1U);
}
static void field_tools_inventory_row(void* context, const char* path, FieldInventoryState state) {
    FieldTools* app = context;
    if(!app->report_ok || state == FieldInventoryUnchanged) return;
    static const char* const names[] = {"UNCHANGED", "CHANGED", "MISSING", "UNREADABLE", "ADDED"};
    char line[288];
    int n = snprintf(line, sizeof(line), "%s\t%s\n", names[state], path);
    app->report_ok = n > 0 && (size_t)n < sizeof(line) &&
                     storage_file_write(app->report, line, n) == (size_t)n;
}
static int32_t field_tools_worker(void* context) {
    FieldTools* app = context;
    switch(app->job) {
    case FieldJobExport:
        app->job_ok =
            field_bundle_export(app->storage, &app->card, app->path, field_tools_progress, app);
        break;
    case FieldJobInspect:
        app->job_ok =
            field_bundle_inspect(app->storage, app->path, &app->card, field_tools_progress, app);
        break;
    case FieldJobImport:
        app->job_ok =
            field_bundle_import(app->storage, app->path, app->edit, field_tools_progress, app);
        break;
    case FieldJobSnapshot:
        app->job_ok = field_inventory_create(app->storage, app->path, field_tools_progress, app);
        break;
    case FieldJobCompare: {
        app->report = storage_file_alloc(app->storage);
        bool created =
            storage_file_open(app->report, app->report_path, FSAM_WRITE, FSOM_CREATE_NEW);
        app->report_ok = created;
        app->job_ok = created && field_inventory_compare(
                                     app->storage,
                                     app->path,
                                     &app->stats,
                                     field_tools_inventory_row,
                                     field_tools_progress,
                                     app);
        app->job_ok = app->job_ok && app->report_ok && storage_file_sync(app->report);
        if(created) app->job_ok = storage_file_close(app->report) && app->job_ok;
        storage_file_free(app->report);
        app->report = NULL;
        if(!app->job_ok && created) storage_common_remove(app->storage, app->report_path);
        break;
    }
    }
    view_dispatcher_send_custom_event(app->dispatcher, FieldJobDone);
    return 0;
}
static void field_tools_start(FieldTools* app, FieldJob job) {
    if(app->thread) return;
    app->job = job;
    app->job_ok = false;
    app->thread = furi_thread_alloc_ex("FieldTools", 8192, field_tools_worker, app);
    field_tools_show(app, FieldPageBusy);
    furi_thread_start(app->thread);
}
static void field_tools_finish(FieldTools* app) {
    if(!app->thread) return;
    furi_thread_join(app->thread);
    furi_thread_free(app->thread);
    app->thread = NULL;
    if(!app->job_ok) {
        field_tools_message(
            app,
            "Cancelled or failed.\nCheck SD / limits.\nExisting captures unchanged.\nIncomplete import copies\nmay remain in imports/.",
            FieldPageHome);
    } else if(app->job == FieldJobInspect) {
        field_tools_show(app, FieldPageBundle);
    } else if(app->job == FieldJobCompare) {
        furi_string_printf(
            app->message,
            "Capture comparison\nOK: %lu  Changed: %lu\nMissing: %lu  New: %lu\nUnreadable: %lu\n\nDifferences report:\n%s\n\nNo files deleted.\nBaseline unchanged.",
            (unsigned long)app->stats.unchanged,
            (unsigned long)app->stats.changed,
            (unsigned long)app->stats.missing,
            (unsigned long)app->stats.added,
            (unsigned long)app->stats.unreadable,
            app->report_path);
        field_tools_message(app, furi_string_get_cstr(app->message), FieldPageHome);
    } else if(app->job == FieldJobImport) {
        field_tools_message(
            app,
            "New object imported.\nPayload copies verified.\nExisting files unchanged.",
            FieldPageHome);
    } else {
        furi_string_printf(
            app->message,
            "%s\n\n%s\n\nCopy this file to iPhone\nwith Companion > Files.\nIt may contain private data.",
            app->job == FieldJobExport ? "Object package verified." : "Capture baseline verified.",
            app->path);
        field_tools_message(app, furi_string_get_cstr(app->message), FieldPageHome);
    }
}
static void field_tools_edit(FieldTools* app, bool object_name) {
    app->back = object_name ? FieldPageBundle : FieldPageItem;
    app->state.page = FieldPageInput;
    memset(app->edit, 0, sizeof(app->edit));
    if(!object_name)
        memcpy(app->edit, app->round.items[app->state.selected].note, sizeof(app->edit));
    text_input_reset(app->input);
    text_input_set_header_text(app->input, object_name ? "New object name" : "Result note");
    text_input_set_minimum_length(app->input, object_name ? 1 : 0);
    text_input_set_result_callback(
        app->input,
        field_tools_name_done,
        app,
        app->edit,
        object_name ? 25 : sizeof(app->edit),
        false);
    view_dispatcher_switch_to_view(app->dispatcher, FieldInputView);
}
static void field_tools_name_done(void* context) {
    field_tools_action(context, FieldNameDone);
}
static bool field_tools_mark(FieldTools* app, FieldRoundResult result, const char* note) {
    FieldRound* next = malloc(sizeof(*next));
    *next = app->round;
    bool ok =
        field_round_mark(next, app->state.selected, result, note, furi_hal_rtc_get_timestamp()) &&
        field_round_save(app->storage, next);
    if(ok) app->round = *next;
    free(next);
    return ok;
}
static void field_tools_export_card(FieldTools* app, const char* name) {
    if(library_card_load(app->storage, name, &app->card) &&
       field_tools_new_path(app, "exports", ".tobj", app->path))
        field_tools_start(app, FieldJobExport);
    else
        field_tools_message(app, "Object / SD read error.\nNothing exported.", FieldPageCards);
}
static bool field_tools_event(void* context, uint32_t event) {
    FieldTools* app = context;
    if(event == FieldJobDone && app->state.page == FieldPageBusy) {
        field_tools_finish(app);
        return true;
    }
    if(event == FieldNameDone && app->state.page == FieldPageInput) {
        if(app->back == FieldPageBundle) {
            if(!library_name_valid(app->edit))
                field_tools_message(
                    app,
                    "Use a new name.\nLetters, digits, spaces,\n- and _; max 24 chars.",
                    FieldPageBundle);
            else
                field_tools_start(app, FieldJobImport);
        } else {
            FieldRoundResult result = app->round.items[app->state.selected].result;
            if(field_tools_mark(app, result, app->edit))
                field_tools_show(app, FieldPageItem);
            else
                field_tools_message(
                    app, "Note not saved.\nPrevious result retained.", FieldPageItem);
        }
        return true;
    }
    if(!field_menu_allows(&app->state, event)) return true;
    if(event == FieldStartRound)
        field_tools_show(app, FieldPageNewConfirm);
    else if(event == FieldResumeRound) {
        if(field_round_load(app->storage, &app->round))
            field_tools_show(app, FieldPageRound);
        else
            field_tools_message(
                app, "No readable saved round.\nStart a round from pins.", FieldPageHome);
    } else if(event == FieldConfirmNew) {
        LibraryPins pins;
        if(library_pins_load(app->storage, &pins) &&
           field_round_begin(&app->round, &pins, furi_hal_rtc_get_timestamp()) &&
           field_round_save(app->storage, &app->round))
            field_tools_show(app, FieldPageRound);
        else
            field_tools_message(
                app, "Pin objects first.\nOr check SD / saved round.", FieldPageHome);
    } else if(event >= FieldRoundBase && event < FieldRoundBase + app->round.count) {
        app->state.selected = event - FieldRoundBase;
        field_tools_show(app, FieldPageItem);
    } else if(event == FieldPass || event == FieldProblem || event == FieldSkip) {
        FieldRoundResult result = event == FieldPass    ? FieldRoundPassed :
                                  event == FieldProblem ? FieldRoundProblem :
                                                          FieldRoundSkipped;
        if(field_tools_mark(app, result, app->round.items[app->state.selected].note))
            field_tools_show(app, FieldPageRound);
        else
            field_tools_message(
                app, "Result not saved.\nPrevious result retained.", FieldPageItem);
    } else if(event == FieldNote) {
        if(app->round.items[app->state.selected].result == FieldRoundPending)
            field_tools_message(
                app,
                "Select a result first.\nOpening a file does not\nmark the check as passed.",
                FieldPageItem);
        else {
            field_tools_edit(app, false);
        }
    } else if(event == FieldOpenObject) {
        const char* name = app->round.items[app->state.selected].object;
        if(library_card_load(app->storage, name, &app->card)) {
            Loader* loader = furi_record_open(RECORD_LOADER);
            FuriString* argument = furi_string_alloc_printf("object:%s", name);
            loader_clear_launch_queue(loader);
            loader_enqueue_launch(
                loader,
                "/ext/apps/Tools/device_library.fap",
                furi_string_get_cstr(argument),
                LoaderDeferredLaunchFlagGui);
            furi_string_free(argument);
            furi_record_close(RECORD_LOADER);
            view_dispatcher_stop(app->dispatcher);
        } else
            field_tools_message(
                app, "Object missing / unreadable.\nRound result unchanged.", FieldPageItem);
    } else if(event == FieldExportRound) {
        if(field_round_load(app->storage, &app->round) &&
           field_round_export(
               app->storage, &app->round, app->report_path, sizeof(app->report_path))) {
            furi_string_printf(
                app->message,
                "Round report written:\n%s\n\nExplicit user results only.",
                app->report_path);
            field_tools_message(app, furi_string_get_cstr(app->message), FieldPageHome);
        } else
            field_tools_message(
                app,
                "Report not written.\nNo round, SD error,\nor report already exists.",
                FieldPageHome);
    } else if(event == FieldExportObject || event == FieldNextCards || event == FieldPreviousCards) {
        if(event == FieldExportObject)
            app->state.offset = 0;
        else if(event == FieldNextCards)
            app->state.offset += 32;
        else
            app->state.offset -= 32;
        field_tools_show(app, FieldPageCards);
    } else if(event >= FieldCardBase && event < FieldCardBase + app->state.count)
        field_tools_export_card(app, app->names[event - FieldCardBase]);
    else if(event == FieldChooseBundle) {
        if(field_tools_choose(app, "/ext", ".tobj")) field_tools_start(app, FieldJobInspect);
    } else if(event == FieldImport) {
        field_tools_edit(app, true);
    } else if(event == FieldInventoryCreate) {
        if(field_tools_new_path(app, "inventories", ".tinv", app->path))
            field_tools_start(app, FieldJobSnapshot);
        else
            field_tools_message(
                app, "Cannot create baseline.\nCheck SD / export space.", FieldPageHome);
    } else if(event == FieldInventoryCompare) {
        if(field_tools_choose(app, LIBRARY_ROOT "/inventories", ".tinv")) {
            if(field_tools_new_path(app, "reports", ".txt", app->report_path))
                field_tools_start(app, FieldJobCompare);
            else
                field_tools_message(
                    app, "Cannot create report.\nExisting files unchanged.", FieldPageHome);
        }
    } else if(event == FieldAbout)
        field_tools_message(
            app,
            "Field tools 1.0\n\nRounds use pinned objects. Results are manual, not inferred from app launch. Export a round before starting another.\n\nObject packages contain private card data and capture bytes. Import creates separate copies; incomplete copies may remain after failure. No scripts or arbitrary FAPs are imported.\n\nInventories: 512 captures, 6 directory levels, 16 MiB per file. SUB / IR / NFC / RFID / iButton roots. Explicit scans only; Back cancels.\n\nAn inventory is not a backup. It cannot recover deleted bytes or prove which application deleted them. Export it off your SD with Companion > Files.",
            FieldPageHome);
    return true;
}
static bool field_tools_back(void* context) {
    FieldTools* app = context;
    if(app->state.page == FieldPageBusy) {
        if(app->thread) furi_thread_flags_set(furi_thread_get_id(app->thread), 1U);
        return true;
    }
    if(app->state.page == FieldPageHome)
        view_dispatcher_stop(app->dispatcher);
    else if(app->state.page == FieldPageMessage || app->state.page == FieldPageInput)
        field_tools_show(app, app->back);
    else if(app->state.page == FieldPageItem)
        field_tools_show(app, FieldPageRound);
    else
        field_tools_show(app, FieldPageHome);
    return true;
}
int32_t field_tools_app(void* context) {
    FieldTools* app = calloc(1, sizeof(*app));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    Gui* gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->text = text_box_alloc();
    app->input = text_input_alloc();
    app->message = furi_string_alloc();
    app->selected_path = furi_string_alloc();
    app->state.round = &app->round;
    app->state.card = &app->card;
    app->state.names = (const char(*)[32])app->names;
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, field_tools_event);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, field_tools_back);
    text_box_set_font(app->text, TextBoxFontText);
    view_dispatcher_add_view(app->dispatcher, FieldMenuView, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, FieldTextView, text_box_get_view(app->text));
    view_dispatcher_add_view(app->dispatcher, FieldInputView, text_input_get_view(app->input));
    view_dispatcher_attach_to_gui(app->dispatcher, gui, ViewDispatcherTypeFullscreen);
    field_tools_show(app, FieldPageHome);
    const char* argument = context;
    if(argument && !strncmp(argument, "export:", 7) && library_name_valid(argument + 7))
        field_tools_export_card(app, argument + 7);
    view_dispatcher_run(app->dispatcher);
    if(app->thread) {
        furi_thread_flags_set(furi_thread_get_id(app->thread), 1U);
        furi_thread_join(app->thread);
        furi_thread_free(app->thread);
    }
    view_dispatcher_remove_view(app->dispatcher, FieldInputView);
    view_dispatcher_remove_view(app->dispatcher, FieldTextView);
    view_dispatcher_remove_view(app->dispatcher, FieldMenuView);
    text_input_free(app->input);
    text_box_free(app->text);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_string_free(app->selected_path);
    furi_string_free(app->message);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
