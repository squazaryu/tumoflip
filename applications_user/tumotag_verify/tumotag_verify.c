#include "tumotag_compare.h"
#include "tumotag_storage.h"

#include <assets_icons.h>
#include <dialogs/dialogs.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <ibutton/ibutton_worker.h>
#include <lfrfid/lfrfid_dict_file.h>
#include <lfrfid/lfrfid_worker.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <loader/loader.h>
#include <nfc/nfc_device.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>

#define TAG                      "TumoTagVerify"
#define TUMOTAG_VERSION          "1.0.0"
#define TUMOTAG_SEARCH_MAX_FILES 256U
#define TUMOTAG_SEARCH_MAX_DEPTH 6U

typedef enum {
    TumoTagViewMain,
    TumoTagViewMedium,
    TumoTagViewScan,
    TumoTagViewResult,
    TumoTagViewAbout,
} TumoTagView;

typedef enum {
    TumoTagMenuVerify,
    TumoTagMenuReadBack,
    TumoTagMenuFind,
    TumoTagMenuLast,
    TumoTagMenuAbout,
} TumoTagMenu;

typedef enum {
    TumoTagEventTick = 100,
    TumoTagEventLfRead,
    TumoTagEventIButtonRead,
    TumoTagEventRetry,
    TumoTagEventDetails,
} TumoTagEvent;

typedef struct {
    TumoTagOperation operation;
    TumoTagMedium medium;
    uint8_t phase;
    char status[40];
} TumoTagScanModel;

typedef struct {
    TumoTagOperation operation;
    TumoTagResult result;
    bool details;
} TumoTagResultModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;
    NotificationApp* notifications;
    Loader* loader;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    Submenu* medium_menu;
    View* scan_view;
    View* result_view;
    Widget* about;
    FuriTimer* ui_timer;
    TumoTagView current_view;

    TumoTagOperation operation;
    TumoTagMedium medium;
    char expected_path[256];
    TumoTagResult result;

    ProtocolDict* expected_lf;
    ProtocolDict* observed_lf;
    ProtocolId expected_lf_protocol;
    ProtocolId observed_lf_protocol;
    LFRFIDWorker* lf_worker;
    bool lf_worker_running;

    iButtonProtocols* ibutton_protocols;
    iButtonKey* expected_ibutton;
    iButtonKey* observed_ibutton;
    iButtonWorker* ibutton_worker;
    bool ibutton_worker_running;
} TumoTagApp;

typedef struct {
    TumoTagApp* app;
    TumoTagMedium medium;
    const char* observed_path;
    uint16_t scanned;
    uint16_t matches;
    TumoTagVerdict best_verdict;
    TumoTagResult best;
    bool has_best;
    bool truncated;
} TumoTagSearch;

static const NotificationSequence tumotag_sequence_scan = {
    &message_blink_start_10,
    &message_blink_set_color_yellow,
    NULL,
};

static const NotificationSequence tumotag_sequence_stop = {
    &message_blink_stop,
    NULL,
};

static const NotificationSequence tumotag_sequence_success = {
    &message_green_255,
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_delay_100,
    &message_green_0,
    NULL,
};

static const NotificationSequence tumotag_sequence_error = {
    &message_red_255,
    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,
    &message_delay_100,
    &message_red_0,
    NULL,
};

static void tumotag_switch_view(TumoTagApp* app, TumoTagView view) {
    app->current_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

static void tumotag_draw_badge(Canvas* canvas, const char* text, bool filled) {
    const uint8_t width = 59U;
    const uint8_t x = 127U - width;
    canvas_draw_rframe(canvas, x, 1, width, 11, 2);
    if(filled) {
        canvas_draw_rbox(canvas, x, 1, width, 11, 2);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x + width / 2U, 10, AlignCenter, AlignBottom, text);
    canvas_set_color(canvas, ColorBlack);
}

static void tumotag_draw_trimmed(Canvas* canvas, uint8_t x, uint8_t y, const char* text) {
    char display[22];
    snprintf(display, sizeof(display), "%s", text ? text : "");
    canvas_draw_str(canvas, x, y, display);
}

static void tumotag_scan_draw(Canvas* canvas, void* context) {
    const TumoTagScanModel* model = context;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoTag");
    tumotag_draw_badge(canvas, "SCANNING", true);
    canvas_draw_line(canvas, 0, 14, 127, 14);

    canvas_set_font(canvas, FontSecondary);
    char operation[48];
    snprintf(
        operation,
        sizeof(operation),
        "%s / %s",
        tumotag_operation_name(model->operation),
        tumotag_medium_name(model->medium));
    tumotag_draw_trimmed(canvas, 2, 25, operation);
    tumotag_draw_trimmed(canvas, 2, 36, model->status);

    canvas_draw_rframe(canvas, 9, 41, 110, 9, 2);
    const uint8_t block_x = 11U + (model->phase % 9U) * 10U;
    canvas_draw_rbox(canvas, block_x, 43, 18, 5, 1);
    elements_button_left(canvas, "Cancel");
}

static bool tumotag_scan_input(InputEvent* event, void* context) {
    UNUSED(context);
    return event->key != InputKeyBack;
}

static void tumotag_result_draw(Canvas* canvas, void* context) {
    const TumoTagResultModel* model = context;
    const TumoTagResult* result = &model->result;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoTag");
    const char* badge = result->verdict == TumoTagVerdictUnsupported ?
                            "UNSUPP." :
                            tumotag_verdict_name(result->verdict);
    tumotag_draw_badge(canvas, badge, result->verdict == TumoTagVerdictVerified);
    canvas_draw_line(canvas, 0, 14, 127, 14);
    canvas_set_font(canvas, FontSecondary);

    if(model->details) {
        char units[48];
        snprintf(
            units,
            sizeof(units),
            "Compared:%u Missing:%u",
            result->compared_units,
            result->missing_units);
        tumotag_draw_trimmed(canvas, 2, 24, result->expected_id);
        tumotag_draw_trimmed(canvas, 2, 34, result->observed_id);
        tumotag_draw_trimmed(canvas, 2, 44, units);
    } else {
        char mode[48];
        snprintf(
            mode,
            sizeof(mode),
            "%s / %s",
            tumotag_operation_name(model->operation),
            tumotag_medium_name(result->medium));
        tumotag_draw_trimmed(canvas, 2, 24, mode);
        char protocols[72];
        snprintf(
            protocols,
            sizeof(protocols),
            "%s -> %s",
            result->expected_protocol[0] ? result->expected_protocol : "-",
            result->observed_protocol[0] ? result->observed_protocol : "-");
        tumotag_draw_trimmed(canvas, 2, 35, protocols);
        tumotag_draw_trimmed(canvas, 2, 46, result->detail);
    }

    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Retry");
    elements_button_right(canvas, model->details ? "Summary" : "Details");
}

static bool tumotag_result_input(InputEvent* event, void* context) {
    TumoTagApp* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk) {
        view_dispatcher_send_custom_event(app->view_dispatcher, TumoTagEventRetry);
        return true;
    }
    if(event->key == InputKeyRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, TumoTagEventDetails);
        return true;
    }
    return false;
}

static void tumotag_scan_model_update(
    TumoTagApp* app,
    TumoTagMedium medium,
    const char* status,
    bool reset_phase) {
    with_view_model(
        app->scan_view,
        TumoTagScanModel * model,
        {
            model->operation = app->operation;
            model->medium = medium;
            if(reset_phase) model->phase = 0U;
            snprintf(model->status, sizeof(model->status), "%s", status);
        },
        true);
}

static void tumotag_result_model_update(TumoTagApp* app, bool details) {
    with_view_model(
        app->result_view,
        TumoTagResultModel * model,
        {
            model->operation = app->operation;
            model->result = app->result;
            model->details = details;
        },
        true);
}

static void tumotag_timer_callback(void* context) {
    TumoTagApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TumoTagEventTick);
}

static void tumotag_stop_scan(TumoTagApp* app) {
    if(app->lf_worker_running) {
        lfrfid_worker_stop(app->lf_worker);
        lfrfid_worker_stop_thread(app->lf_worker);
        app->lf_worker_running = false;
    }
    if(app->ibutton_worker_running) {
        ibutton_worker_stop(app->ibutton_worker);
        ibutton_worker_stop_thread(app->ibutton_worker);
        app->ibutton_worker_running = false;
    }
    furi_timer_stop(app->ui_timer);
    notification_message(app->notifications, &tumotag_sequence_stop);
}

static bool tumotag_has_extension(const char* path, const char* extension) {
    const size_t path_length = strlen(path);
    const size_t extension_length = strlen(extension);
    return path_length > extension_length &&
           strcmp(path + path_length - extension_length, extension) == 0;
}

static bool tumotag_search_compare_candidate(
    TumoTagSearch* search,
    const char* candidate_path,
    TumoTagResult* candidate) {
    TumoTagApp* app = search->app;
    bool parsed = false;
    if(search->medium == TumoTagMediumNfc) {
        parsed = tumotag_compare_nfc_files(candidate_path, search->observed_path, candidate);
    } else if(search->medium == TumoTagMediumLfRfid) {
        ProtocolDict* expected = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
        const ProtocolId protocol = lfrfid_dict_file_load(expected, candidate_path);
        if(protocol != PROTOCOL_NO) {
            parsed = tumotag_compare_lfrfid(
                expected, protocol, app->observed_lf, app->observed_lf_protocol, candidate);
        }
        protocol_dict_free(expected);
    } else {
        iButtonKey* expected =
            ibutton_key_alloc(ibutton_protocols_get_max_data_size(app->ibutton_protocols));
        if(ibutton_protocols_load(app->ibutton_protocols, expected, candidate_path)) {
            parsed = tumotag_compare_ibutton(
                app->ibutton_protocols, expected, app->observed_ibutton, candidate);
        }
        ibutton_key_free(expected);
    }
    if(parsed)
        snprintf(candidate->expected_path, sizeof(candidate->expected_path), "%s", candidate_path);
    return parsed;
}

static uint8_t tumotag_verdict_priority(TumoTagVerdict verdict) {
    if(verdict == TumoTagVerdictVerified) return 2U;
    if(verdict == TumoTagVerdictPartial) return 1U;
    return 0U;
}

static void tumotag_search_consider(
    TumoTagSearch* search,
    const char* candidate_path,
    const TumoTagResult* candidate) {
    const uint8_t priority = tumotag_verdict_priority(candidate->verdict);
    if(priority == 0U) return;

    if(!search->has_best || priority > tumotag_verdict_priority(search->best_verdict)) {
        search->best = *candidate;
        search->best_verdict = candidate->verdict;
        search->has_best = true;
        search->matches = 1U;
    } else if(priority == tumotag_verdict_priority(search->best_verdict)) {
        search->matches++;
        if(strcmp(candidate_path, search->best.expected_path) < 0) {
            search->best = *candidate;
            search->best_verdict = candidate->verdict;
        }
    }
}

static void
    tumotag_search_directory(TumoTagSearch* search, const char* directory_path, uint8_t depth) {
    if(depth > TUMOTAG_SEARCH_MAX_DEPTH || search->truncated) return;

    File* directory = storage_file_alloc(search->app->storage);
    if(!storage_dir_open(directory, directory_path)) {
        storage_file_free(directory);
        return;
    }

    FileInfo info;
    char name[96];
    while(storage_dir_read(directory, &info, name, sizeof(name))) {
        if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char path[256];
        const int length = snprintf(path, sizeof(path), "%s/%s", directory_path, name);
        if(length <= 0 || (size_t)length >= sizeof(path)) continue;

        if(file_info_is_dir(&info)) {
            tumotag_search_directory(search, path, depth + 1U);
            if(search->truncated) break;
        } else if(tumotag_has_extension(path, tumotag_medium_extension(search->medium))) {
            if(search->scanned >= TUMOTAG_SEARCH_MAX_FILES) {
                search->truncated = true;
                break;
            }
            search->scanned++;
            TumoTagResult candidate;
            if(tumotag_search_compare_candidate(search, path, &candidate)) {
                tumotag_search_consider(search, path, &candidate);
            }
        }
    }

    storage_dir_close(directory);
    storage_file_free(directory);
}

static void tumotag_seed_observed_result(TumoTagApp* app, TumoTagResult* result) {
    if(app->medium == TumoTagMediumNfc) {
        tumotag_compare_nfc_files(TUMOTAG_NFC_OBSERVED_PATH, TUMOTAG_NFC_OBSERVED_PATH, result);
    } else if(app->medium == TumoTagMediumLfRfid) {
        tumotag_compare_lfrfid(
            app->observed_lf,
            app->observed_lf_protocol,
            app->observed_lf,
            app->observed_lf_protocol,
            result);
    } else {
        tumotag_compare_ibutton(
            app->ibutton_protocols, app->observed_ibutton, app->observed_ibutton, result);
    }
    result->expected_protocol[0] = '\0';
    result->expected_id[0] = '\0';
}

static void tumotag_find_matches(TumoTagApp* app) {
    TumoTagSearch search = {
        .app = app,
        .medium = app->medium,
        .observed_path = app->medium == TumoTagMediumNfc ? TUMOTAG_NFC_OBSERVED_PATH : NULL,
        .best_verdict = TumoTagVerdictUnsupported,
    };
    tumotag_search_directory(&search, tumotag_medium_root(app->medium), 0U);

    if(search.has_best) {
        app->result = search.best;
        snprintf(
            app->result.match_path,
            sizeof(app->result.match_path),
            "%s",
            search.best.expected_path);
        app->result.duplicate_matches = search.matches > 0U ? search.matches - 1U : 0U;
    } else {
        tumotag_seed_observed_result(app, &app->result);
        app->result.verdict = TumoTagVerdictDifferent;
        snprintf(
            app->result.detail,
            sizeof(app->result.detail),
            "No match in %u saved files",
            search.scanned);
    }
    app->result.search_truncated = search.truncated;
}

static void tumotag_present_result(TumoTagApp* app, bool save_report) {
    tumotag_stop_scan(app);
    if(save_report) {
        tumotag_report_save(app->storage, app->operation, &app->result, NULL, 0U);
    }
    tumotag_result_model_update(app, false);
    notification_message(
        app->notifications,
        app->result.verdict == TumoTagVerdictVerified ? &tumotag_sequence_success :
                                                        &tumotag_sequence_error);
    tumotag_switch_view(app, TumoTagViewResult);
}

static void tumotag_make_error(
    TumoTagApp* app,
    TumoTagMedium medium,
    const char* detail,
    bool save_report) {
    tumotag_result_reset(&app->result, medium);
    app->result.verdict = TumoTagVerdictUnsupported;
    snprintf(app->result.detail, sizeof(app->result.detail), "%s", detail);
    snprintf(
        app->result.expected_path, sizeof(app->result.expected_path), "%s", app->expected_path);
    tumotag_present_result(app, save_report);
}

static void
    tumotag_lf_read_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    TumoTagApp* app = context;
    if(result == LFRFIDWorkerReadDone) {
        app->observed_lf_protocol = protocol;
        view_dispatcher_send_custom_event(app->view_dispatcher, TumoTagEventLfRead);
    }
}

static void tumotag_ibutton_read_callback(void* context) {
    TumoTagApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TumoTagEventIButtonRead);
}

static void tumotag_start_direct_scan(TumoTagApp* app) {
    tumotag_stop_scan(app);
    tumotag_scan_model_update(
        app,
        app->medium,
        app->medium == TumoTagMediumLfRfid ? "Present LF RFID tag" : "Touch iButton contact",
        true);
    notification_message(app->notifications, &tumotag_sequence_scan);
    furi_timer_start(app->ui_timer, furi_ms_to_ticks(180U));
    tumotag_switch_view(app, TumoTagViewScan);

    if(app->medium == TumoTagMediumLfRfid) {
        app->lf_worker_running = true;
        lfrfid_worker_start_thread(app->lf_worker);
        lfrfid_worker_read_start(
            app->lf_worker, LFRFIDWorkerReadTypeAuto, tumotag_lf_read_callback, app);
    } else {
        ibutton_key_reset(app->observed_ibutton);
        app->ibutton_worker_running = true;
        ibutton_worker_start_thread(app->ibutton_worker);
        ibutton_worker_read_set_callback(app->ibutton_worker, tumotag_ibutton_read_callback, app);
        ibutton_worker_read_start(app->ibutton_worker, app->observed_ibutton);
    }
}

static bool tumotag_validate_expected(TumoTagApp* app) {
    if(app->operation == TumoTagOperationFind) return true;
    if(app->medium == TumoTagMediumNfc) {
        NfcDevice* device = nfc_device_alloc();
        const bool loaded = nfc_device_load(device, app->expected_path);
        nfc_device_free(device);
        return loaded;
    }
    if(app->medium == TumoTagMediumLfRfid) {
        app->expected_lf_protocol = lfrfid_dict_file_load(app->expected_lf, app->expected_path);
        return app->expected_lf_protocol != PROTOCOL_NO;
    }
    ibutton_key_reset(app->expected_ibutton);
    return ibutton_protocols_load(
        app->ibutton_protocols, app->expected_ibutton, app->expected_path);
}

static bool tumotag_select_expected(TumoTagApp* app) {
    FuriString* path = furi_string_alloc_set(tumotag_medium_root(app->medium));
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, tumotag_medium_extension(app->medium), NULL);
    options.base_path = tumotag_medium_root(app->medium);
    const bool selected = dialog_file_browser_show(app->dialogs, path, path, &options);
    if(selected && furi_string_size(path) < sizeof(app->expected_path)) {
        snprintf(app->expected_path, sizeof(app->expected_path), "%s", furi_string_get_cstr(path));
    }
    furi_string_free(path);
    return selected;
}

static void tumotag_start_nfc_capture(TumoTagApp* app) {
    FuriString* self_path = furi_string_alloc();
    const bool self_found = loader_get_application_launch_path(app->loader, self_path);
    TumoTagRoute route = {
        .operation = app->operation,
        .medium = TumoTagMediumNfc,
    };
    snprintf(route.expected_path, sizeof(route.expected_path), "%s", app->expected_path);

    storage_common_remove(app->storage, TUMOTAG_NFC_OBSERVED_PATH);
    if(!self_found || !tumotag_route_prepare(app->storage, &route)) {
        furi_string_free(self_path);
        tumotag_make_error(app, TumoTagMediumNfc, "Could not prepare NFC reader route", true);
        return;
    }

    loader_clear_launch_queue(app->loader);
    loader_enqueue_launch(app->loader, "NFC", TUMOTAG_NFC_LAUNCH_ARG, LoaderDeferredLaunchFlagGui);
    loader_enqueue_launch(
        app->loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    furi_string_free(self_path);
    view_dispatcher_stop(app->view_dispatcher);
}

static void tumotag_begin_operation(TumoTagApp* app) {
    if(app->operation != TumoTagOperationFind && !tumotag_select_expected(app)) return;
    if(!tumotag_validate_expected(app)) {
        tumotag_make_error(app, app->medium, "Unsupported or damaged saved file", true);
        return;
    }
    if(app->medium == TumoTagMediumNfc) {
        tumotag_start_nfc_capture(app);
    } else {
        tumotag_start_direct_scan(app);
    }
}

static void tumotag_complete_direct_scan(TumoTagApp* app) {
    tumotag_stop_scan(app);
    if(app->operation == TumoTagOperationFind) {
        tumotag_find_matches(app);
    } else if(app->medium == TumoTagMediumLfRfid) {
        tumotag_compare_lfrfid(
            app->expected_lf,
            app->expected_lf_protocol,
            app->observed_lf,
            app->observed_lf_protocol,
            &app->result);
        snprintf(
            app->result.expected_path, sizeof(app->result.expected_path), "%s", app->expected_path);
    } else {
        tumotag_compare_ibutton(
            app->ibutton_protocols, app->expected_ibutton, app->observed_ibutton, &app->result);
        snprintf(
            app->result.expected_path, sizeof(app->result.expected_path), "%s", app->expected_path);
    }
    tumotag_present_result(app, true);
}

static void tumotag_resume_nfc_route(TumoTagApp* app, const TumoTagRoute* route) {
    app->operation = route->operation;
    app->medium = route->medium;
    snprintf(app->expected_path, sizeof(app->expected_path), "%s", route->expected_path);
    if(!storage_file_exists(app->storage, TUMOTAG_NFC_OBSERVED_PATH)) {
        tumotag_make_error(app, TumoTagMediumNfc, "NFC read cancelled or no tag", true);
        return;
    }

    if(app->operation == TumoTagOperationFind) {
        tumotag_find_matches(app);
    } else {
        tumotag_compare_nfc_files(app->expected_path, TUMOTAG_NFC_OBSERVED_PATH, &app->result);
    }
    storage_common_remove(app->storage, TUMOTAG_NFC_OBSERVED_PATH);
    tumotag_present_result(app, true);
}

static void tumotag_medium_callback(void* context, uint32_t index);

static void tumotag_open_medium_menu(TumoTagApp* app, TumoTagOperation operation) {
    app->operation = operation;
    submenu_reset(app->medium_menu);
    submenu_set_header(app->medium_menu, tumotag_operation_name(operation));
    submenu_add_item(app->medium_menu, "NFC", TumoTagMediumNfc, tumotag_medium_callback, app);
    submenu_add_item(
        app->medium_menu, "LF RFID", TumoTagMediumLfRfid, tumotag_medium_callback, app);
    submenu_add_item(
        app->medium_menu, "iButton", TumoTagMediumIButton, tumotag_medium_callback, app);
}

static void tumotag_medium_callback(void* context, uint32_t index) {
    TumoTagApp* app = context;
    if(index > TumoTagMediumIButton) return;
    app->medium = index;
    tumotag_begin_operation(app);
}

static void tumotag_open_medium(TumoTagApp* app, TumoTagOperation operation) {
    tumotag_open_medium_menu(app, operation);
    tumotag_switch_view(app, TumoTagViewMedium);
}

static void tumotag_open_about(TumoTagApp* app) {
    widget_reset(app->about);
    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "TumoTag Verify v" TUMOTAG_VERSION "\n"
        "github.com/squazaryu/tumoflip #171\n\n"
        "Compares a physical NFC, LF RFID or iButton token with a saved artifact. "
        "Verified requires protocol and readable data equality. Partial never proves "
        "unreadable or protected regions. Compare and Find never modify tokens or source files.");
    tumotag_switch_view(app, TumoTagViewAbout);
}

static void tumotag_main_callback(void* context, uint32_t index) {
    TumoTagApp* app = context;
    switch(index) {
    case TumoTagMenuVerify:
        tumotag_open_medium(app, TumoTagOperationVerify);
        break;
    case TumoTagMenuReadBack:
        tumotag_open_medium(app, TumoTagOperationReadBack);
        break;
    case TumoTagMenuFind:
        tumotag_open_medium(app, TumoTagOperationFind);
        break;
    case TumoTagMenuLast:
        if(tumotag_report_load_last(app->storage, &app->operation, &app->result)) {
            app->medium = app->result.medium;
            snprintf(
                app->expected_path, sizeof(app->expected_path), "%s", app->result.expected_path);
            tumotag_present_result(app, false);
        } else {
            tumotag_make_error(app, TumoTagMediumNfc, "No saved result yet", false);
        }
        break;
    case TumoTagMenuAbout:
        tumotag_open_about(app);
        break;
    default:
        break;
    }
}

static void tumotag_configure_main_menu(TumoTagApp* app) {
    submenu_set_header(app->main_menu, "TumoTag Verify");
    submenu_add_item(
        app->main_menu, "Verify saved artifact", TumoTagMenuVerify, tumotag_main_callback, app);
    submenu_add_item(
        app->main_menu, "Verify after write", TumoTagMenuReadBack, tumotag_main_callback, app);
    submenu_add_item(
        app->main_menu, "Find saved token", TumoTagMenuFind, tumotag_main_callback, app);
    submenu_add_item(app->main_menu, "Last result", TumoTagMenuLast, tumotag_main_callback, app);
    submenu_add_item(app->main_menu, "About", TumoTagMenuAbout, tumotag_main_callback, app);
}

static bool tumotag_custom_event_callback(void* context, uint32_t event) {
    TumoTagApp* app = context;
    if(event == TumoTagEventTick && app->current_view == TumoTagViewScan) {
        with_view_model(
            app->scan_view,
            TumoTagScanModel * model,
            { model->phase = (model->phase + 1U) % 9U; },
            true);
        return true;
    }
    if((event == TumoTagEventLfRead || event == TumoTagEventIButtonRead) &&
       app->current_view == TumoTagViewScan) {
        tumotag_complete_direct_scan(app);
        return true;
    }
    if(event == TumoTagEventRetry && app->current_view == TumoTagViewResult) {
        if(app->medium == TumoTagMediumNfc) {
            tumotag_start_nfc_capture(app);
        } else {
            tumotag_start_direct_scan(app);
        }
        return true;
    }
    if(event == TumoTagEventDetails && app->current_view == TumoTagViewResult) {
        with_view_model(
            app->result_view,
            TumoTagResultModel * model,
            { model->details = !model->details; },
            true);
        return true;
    }
    return false;
}

static bool tumotag_navigation_callback(void* context) {
    TumoTagApp* app = context;
    if(app->current_view == TumoTagViewMain) {
        view_dispatcher_stop(app->view_dispatcher);
    } else if(app->current_view == TumoTagViewScan) {
        tumotag_stop_scan(app);
        tumotag_switch_view(app, TumoTagViewMedium);
    } else if(app->current_view == TumoTagViewMedium) {
        tumotag_switch_view(app, TumoTagViewMain);
    } else {
        tumotag_switch_view(app, TumoTagViewMain);
    }
    return true;
}

static TumoTagApp* tumotag_app_alloc(void) {
    TumoTagApp* app = malloc(sizeof(TumoTagApp));
    memset(app, 0, sizeof(*app));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->loader = furi_record_open(RECORD_LOADER);
    tumotag_storage_prepare(app->storage);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, tumotag_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, tumotag_navigation_callback);

    app->main_menu = submenu_alloc();
    app->medium_menu = submenu_alloc();
    app->scan_view = view_alloc();
    view_allocate_model(app->scan_view, ViewModelTypeLocking, sizeof(TumoTagScanModel));
    view_set_context(app->scan_view, app);
    view_set_draw_callback(app->scan_view, tumotag_scan_draw);
    view_set_input_callback(app->scan_view, tumotag_scan_input);
    app->result_view = view_alloc();
    view_allocate_model(app->result_view, ViewModelTypeLocking, sizeof(TumoTagResultModel));
    view_set_context(app->result_view, app);
    view_set_draw_callback(app->result_view, tumotag_result_draw);
    view_set_input_callback(app->result_view, tumotag_result_input);
    app->about = widget_alloc();

    view_dispatcher_add_view(
        app->view_dispatcher, TumoTagViewMain, submenu_get_view(app->main_menu));
    view_dispatcher_add_view(
        app->view_dispatcher, TumoTagViewMedium, submenu_get_view(app->medium_menu));
    view_dispatcher_add_view(app->view_dispatcher, TumoTagViewScan, app->scan_view);
    view_dispatcher_add_view(app->view_dispatcher, TumoTagViewResult, app->result_view);
    view_dispatcher_add_view(app->view_dispatcher, TumoTagViewAbout, widget_get_view(app->about));

    app->ui_timer = furi_timer_alloc(tumotag_timer_callback, FuriTimerTypePeriodic, app);
    app->expected_lf = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    app->observed_lf = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    app->lf_worker = lfrfid_worker_alloc(app->observed_lf);
    app->ibutton_protocols = ibutton_protocols_alloc();
    const size_t ibutton_size = ibutton_protocols_get_max_data_size(app->ibutton_protocols);
    app->expected_ibutton = ibutton_key_alloc(ibutton_size);
    app->observed_ibutton = ibutton_key_alloc(ibutton_size);
    app->ibutton_worker = ibutton_worker_alloc(app->ibutton_protocols);

    tumotag_configure_main_menu(app);
    return app;
}

static void tumotag_app_free(TumoTagApp* app) {
    tumotag_stop_scan(app);
    furi_timer_free(app->ui_timer);

    ibutton_worker_free(app->ibutton_worker);
    ibutton_key_free(app->observed_ibutton);
    ibutton_key_free(app->expected_ibutton);
    ibutton_protocols_free(app->ibutton_protocols);
    lfrfid_worker_free(app->lf_worker);
    protocol_dict_free(app->observed_lf);
    protocol_dict_free(app->expected_lf);

    view_dispatcher_remove_view(app->view_dispatcher, TumoTagViewAbout);
    view_dispatcher_remove_view(app->view_dispatcher, TumoTagViewResult);
    view_dispatcher_remove_view(app->view_dispatcher, TumoTagViewScan);
    view_dispatcher_remove_view(app->view_dispatcher, TumoTagViewMedium);
    view_dispatcher_remove_view(app->view_dispatcher, TumoTagViewMain);
    widget_free(app->about);
    view_free(app->result_view);
    view_free(app->scan_view);
    submenu_free(app->medium_menu);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tumotag_verify_app(void* context) {
    UNUSED(context);
    TumoTagApp* app = tumotag_app_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    TumoTagRoute route;
    if(tumotag_route_resume(app->storage, &route) && route.medium == TumoTagMediumNfc) {
        tumotag_resume_nfc_route(app, &route);
    } else {
        tumotag_switch_view(app, TumoTagViewMain);
    }

    view_dispatcher_run(app->view_dispatcher);
    tumotag_app_free(app);
    return 0;
}
