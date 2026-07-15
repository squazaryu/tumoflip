#include "tumospectrum_analysis.h"
#include "tumospectrum_capture_flow.h"
#include "tumospectrum_inference.h"
#include "tumospectrum_parser.h"
#include "tumospectrum_storage.h"
#include "signal_workbench_icons.h"

#include <bt/bt_service/bt.h>
#include <dialogs/dialogs.h>
#include <furi.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <loader/loader.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUMOSPECTRUM_SUBGHZ_DIR     EXT_PATH("subghz")
#define TUMOSPECTRUM_INFRARED_DIR   EXT_PATH("infrared")
#define TUMOSPECTRUM_SCOPE_DIR      EXT_PATH("apps_data/tumoscope/captures")
#define TUMOSPECTRUM_BRIDGE_APP_ID  "signal_workbench"
#define TUMOSPECTRUM_BRIDGE_COMMAND "report"

typedef enum {
    TumoSpectrumViewMenu,
    TumoSpectrumViewResult,
    TumoSpectrumViewActions,
    TumoSpectrumViewText,
    TumoSpectrumViewNote,
    TumoSpectrumViewCaptureSet,
    TumoSpectrumViewSetInput,
} TumoSpectrumView;

typedef enum {
    TumoSpectrumMenuCaptureSubGhz,
    TumoSpectrumMenuCaptureInfrared,
    TumoSpectrumMenuOpenSubGhz,
    TumoSpectrumMenuOpenInfrared,
    TumoSpectrumMenuNewSubGhzSet,
    TumoSpectrumMenuNewInfraredSet,
    TumoSpectrumMenuLatestSet,
    TumoSpectrumMenuOpenScope,
    TumoSpectrumMenuImportAnalyzer,
    TumoSpectrumMenuNotebook,
    TumoSpectrumMenuAbout,
} TumoSpectrumMenuAction;

typedef enum {
    TumoSpectrumActionNote,
    TumoSpectrumActionSave,
    TumoSpectrumActionCompare,
    TumoSpectrumActionHandoff,
    TumoSpectrumActionCompanion,
    TumoSpectrumActionDetails,
} TumoSpectrumResultAction;

typedef enum {
    TumoSpectrumSetInputDevice,
    TumoSpectrumSetInputControl,
} TumoSpectrumSetInputStage;

typedef struct TumoSpectrumApp TumoSpectrumApp;

typedef struct {
    TumoSpectrumApp* app;
    uint8_t page;
} TumoSpectrumResultModel;

typedef struct {
    TumoSpectrumApp* app;
} TumoSpectrumSetModel;

struct TumoSpectrumApp {
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;
    Loader* loader;
    Bt* bt;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    Submenu* menu;
    Submenu* actions;
    View* result_view;
    View* capture_set_view;
    TextBox* text_box;
    TextInput* note_input;
    TextInput* set_input;
    FuriString* text;
    TumoSpectrumCapture capture;
    TumoSpectrumCapture compared;
    TumoSpectrumComparison comparison;
    char note_buffer[TUMOSPECTRUM_NOTE_SIZE];
    char report_path[TUMOSPECTRUM_PATH_SIZE];
    TumoSpectrumCaptureSet capture_set;
    TumoSpectrumSetInputStage set_input_stage;
    char set_input_buffer[TUMOSPECTRUM_SET_LABEL_SIZE];
    char set_profile_path[TUMOSPECTRUM_PATH_SIZE];
    char set_report_path[TUMOSPECTRUM_PATH_SIZE];
    uint8_t result_page;
};

static const NotificationSequence tumospectrum_sequence_ok = {
    &message_display_backlight_on,
    &message_green_255,
    &message_delay_10,
    NULL,
};

static const NotificationSequence tumospectrum_sequence_error = {
    &message_display_backlight_on,
    &message_red_255,
    &message_delay_10,
    NULL,
};

static void tumospectrum_refresh_result(TumoSpectrumApp* app) {
    with_view_model(
        app->result_view,
        TumoSpectrumResultModel * model,
        {
            model->app = app;
            model->page = app->result_page;
        },
        true);
}

static void tumospectrum_refresh_set(TumoSpectrumApp* app) {
    with_view_model(
        app->capture_set_view, TumoSpectrumSetModel * model, { model->app = app; }, true);
}

static const char* tumospectrum_badge(TumoSpectrumCaptureType type) {
    switch(type) {
    case TumoSpectrumCaptureSubGhzRaw:
        return "SUB RAW";
    case TumoSpectrumCaptureSubGhzDecoded:
        return "SUB KEY";
    case TumoSpectrumCaptureInfraredRaw:
        return "IR RAW";
    case TumoSpectrumCaptureInfraredParsed:
        return "IR KEY";
    case TumoSpectrumCaptureTumoScope:
        return "GPIO";
    case TumoSpectrumCaptureFrequencyObservation:
        return "RF NOTE";
    default:
        return "EMPTY";
    }
}

static void tumospectrum_draw_badge(Canvas* canvas, const char* text) {
    canvas_draw_rframe(canvas, 80, 0, 48, 13, 2);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 104, 10, AlignCenter, AlignBottom, text);
}

static void tumospectrum_draw_summary(Canvas* canvas, const TumoSpectrumCapture* capture) {
    char line[64];
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Name: %.18s", capture->name[0] ? capture->name : "--");
    canvas_draw_str(canvas, 2, 25, line);
    if(capture->frequency_hz >= 1000000U) {
        snprintf(
            line,
            sizeof(line),
            "Freq: %lu.%03lu MHz",
            (unsigned long)(capture->frequency_hz / 1000000U),
            (unsigned long)((capture->frequency_hz % 1000000U) / 1000U));
    } else if(capture->frequency_hz > 0U) {
        snprintf(line, sizeof(line), "Freq: %lu Hz", (unsigned long)capture->frequency_hz);
    } else {
        strlcpy(line, "Freq: --", sizeof(line));
    }
    canvas_draw_str(canvas, 2, 36, line);
    snprintf(
        line,
        sizeof(line),
        "%s: %.17s",
        capture->protocol[0] ? "Protocol" : "Status",
        capture->protocol[0] ? capture->protocol : tumospectrum_status_name(capture->status));
    canvas_draw_str(canvas, 2, 47, line);
}

static void tumospectrum_draw_stats(Canvas* canvas, const TumoSpectrumCapture* capture) {
    char line[64];
    const TumoSpectrumAnalysis* analysis = &capture->analysis;
    canvas_set_font(canvas, FontSecondary);
    snprintf(
        line,
        sizeof(line),
        "Pulse: %lu%s  Burst: %u",
        (unsigned long)analysis->count,
        capture->truncated ? "+" : "",
        analysis->burst_count);
    canvas_draw_str(canvas, 2, 23, line);
    snprintf(
        line,
        sizeof(line),
        "Min/Avg/Max %lu/%lu/%lu us",
        (unsigned long)analysis->minimum_us,
        (unsigned long)analysis->average_us,
        (unsigned long)analysis->maximum_us);
    canvas_draw_str(canvas, 2, 33, line);
    snprintf(
        line,
        sizeof(line),
        "Duration %llu us  Repeat %u%%",
        (unsigned long long)analysis->duration_us,
        analysis->repeat_score);
    canvas_draw_str(canvas, 2, 43, line);
    snprintf(line, sizeof(line), "Candidate: %.18s", analysis->candidate);
    canvas_draw_str(canvas, 2, 51, line);
}

static void tumospectrum_draw_histogram(Canvas* canvas, const TumoSpectrumCapture* capture) {
    const TumoSpectrumAnalysis* analysis = &capture->analysis;
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 23, "Pulse / gap distribution");
    canvas_draw_line(canvas, 8, 49, 120, 49);
    for(size_t index = 0U; index < TUMOSPECTRUM_HISTOGRAM_BUCKETS; index++) {
        const uint8_t height = (uint8_t)(analysis->histogram[index] * 22U / 100U);
        const uint8_t x = (uint8_t)(10U + index * 14U);
        if(height > 0U) canvas_draw_box(canvas, x, (uint8_t)(48U - height), 9, height);
    }
}

static void tumospectrum_draw_comparison(Canvas* canvas, const TumoSpectrumApp* app) {
    char line[64];
    canvas_set_font(canvas, FontSecondary);
    if(!app->comparison.compatible) {
        canvas_draw_str(canvas, 2, 29, "No compatible comparison");
        canvas_draw_str(canvas, 2, 41, "Select second RAW");
        return;
    }
    snprintf(
        line,
        sizeof(line),
        "%s  Similarity %u%%",
        app->comparison.likely_same ? "Likely same" : "Different",
        app->comparison.overall_similarity);
    canvas_draw_str(canvas, 2, 25, line);
    snprintf(
        line,
        sizeof(line),
        "Histogram %u%%  dF %+ld Hz",
        app->comparison.histogram_similarity,
        (long)app->comparison.frequency_delta_hz);
    canvas_draw_str(canvas, 2, 36, line);
    snprintf(
        line,
        sizeof(line),
        "dPulse %+ld  dTime %+ld%%",
        (long)app->comparison.pulse_delta,
        (long)app->comparison.duration_delta_percent);
    canvas_draw_str(canvas, 2, 47, line);
}

static void tumospectrum_result_draw(Canvas* canvas, void* context) {
    const TumoSpectrumResultModel* model = context;
    const TumoSpectrumApp* app = model->app;
    const TumoSpectrumCapture* capture = &app->capture;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "TumoSpectrum");
    tumospectrum_draw_badge(canvas, tumospectrum_badge(capture->type));
    canvas_draw_line(canvas, 0, 15, 127, 15);

    if(capture->status != TumoSpectrumStatusOk) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 4, 30, tumospectrum_status_name(capture->status));
        canvas_draw_str(canvas, 4, 43, "File was not modified");
    } else if(model->page == 0U) {
        tumospectrum_draw_summary(canvas, capture);
    } else if(model->page == 1U && tumospectrum_type_has_timings(capture->type)) {
        tumospectrum_draw_stats(canvas, capture);
    } else if(model->page == 2U && tumospectrum_type_has_timings(capture->type)) {
        tumospectrum_draw_histogram(canvas, capture);
    } else {
        tumospectrum_draw_comparison(canvas, app);
    }

    const bool compare_page = tumospectrum_type_has_timings(capture->type) && model->page == 3U;
    elements_button_left(canvas, "Prev");
    elements_button_center(canvas, capture->status == TumoSpectrumStatusOk ? "Actions" : "Menu");
    elements_button_right(canvas, compare_page ? "Compare" : "Next");
}

static uint8_t tumospectrum_page_count(const TumoSpectrumCapture* capture) {
    return tumospectrum_type_has_timings(capture->type) ? 4U : 1U;
}

static void tumospectrum_build_actions(TumoSpectrumApp* app);
static void tumospectrum_compare_capture(TumoSpectrumApp* app);

static bool tumospectrum_result_input(InputEvent* event, void* context) {
    TumoSpectrumApp* app = context;
    if(event->type != InputTypeShort) return false;
    const uint8_t page_count = tumospectrum_page_count(&app->capture);
    if(event->key == InputKeyLeft) {
        app->result_page = app->result_page == 0U ? page_count - 1U : app->result_page - 1U;
    } else if(event->key == InputKeyRight) {
        if(tumospectrum_type_has_timings(app->capture.type) && app->result_page == 3U) {
            tumospectrum_compare_capture(app);
            return true;
        }
        app->result_page = (uint8_t)((app->result_page + 1U) % page_count);
    } else if(event->key == InputKeyOk) {
        if(app->capture.status == TumoSpectrumStatusOk) {
            tumospectrum_build_actions(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewActions);
        } else {
            view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewMenu);
        }
        return true;
    } else {
        return false;
    }
    tumospectrum_refresh_result(app);
    return true;
}

static void tumospectrum_capture_set_add_sample(TumoSpectrumApp* app);
static void tumospectrum_capture_set_infer(TumoSpectrumApp* app);
static void tumospectrum_capture_set_show_details(TumoSpectrumApp* app);
static void tumospectrum_capture_set_send(TumoSpectrumApp* app);

static void tumospectrum_capture_set_draw(Canvas* canvas, void* context) {
    const TumoSpectrumSetModel* model = context;
    const TumoSpectrumApp* app = model->app;
    const TumoSpectrumCaptureSet* capture_set = &app->capture_set;
    char line[64];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Capture Set");
    tumospectrum_draw_badge(
        canvas, capture_set->type == TumoSpectrumCaptureSubGhzRaw ? "SUB RAW" : "IR RAW");
    canvas_draw_line(canvas, 0, 15, 127, 15);
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Device: %.18s", capture_set->device_label);
    canvas_draw_str(canvas, 2, 25, line);
    snprintf(line, sizeof(line), "Control: %.17s", capture_set->control_label);
    canvas_draw_str(canvas, 2, 35, line);
    if(capture_set->inferred) {
        snprintf(
            line,
            sizeof(line),
            "%u samples  stable %u%%",
            (unsigned int)capture_set->sample_count,
            capture_set->inference.stable_percent);
        canvas_draw_str(canvas, 2, 45, line);
        canvas_draw_str(
            canvas, 2, 53, tumospectrum_replay_class_name(capture_set->inference.replay_class));
        elements_button_center(canvas, "Details");
        elements_button_right(canvas, "Send");
    } else {
        snprintf(
            line,
            sizeof(line),
            "Samples %u/%u",
            (unsigned int)capture_set->sample_count,
            TUMOSPECTRUM_SET_MAX_SAMPLES);
        canvas_draw_str(canvas, 2, 45, line);
        canvas_draw_str(
            canvas,
            2,
            53,
            capture_set->sample_count < TUMOSPECTRUM_SET_MIN_SAMPLES ? "Add at least 3 RAW files" :
                                                                       "Ready to infer");
        elements_button_center(canvas, "Add");
        if(capture_set->sample_count >= TUMOSPECTRUM_SET_MIN_SAMPLES) {
            elements_button_right(canvas, "Infer");
        }
    }
    elements_button_left(canvas, "Back");
}

static bool tumospectrum_capture_set_input(InputEvent* event, void* context) {
    TumoSpectrumApp* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyLeft || event->key == InputKeyBack) {
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewMenu);
        return true;
    }
    if(event->key == InputKeyOk) {
        if(app->capture_set.inferred) {
            tumospectrum_capture_set_show_details(app);
        } else {
            tumospectrum_capture_set_add_sample(app);
        }
        return true;
    }
    if(event->key == InputKeyRight) {
        if(app->capture_set.inferred) {
            tumospectrum_capture_set_send(app);
        } else if(app->capture_set.sample_count >= TUMOSPECTRUM_SET_MIN_SAMPLES) {
            tumospectrum_capture_set_infer(app);
        }
        return true;
    }
    return false;
}

static uint32_t tumospectrum_result_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewMenu;
}

static uint32_t tumospectrum_actions_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewResult;
}

static uint32_t tumospectrum_text_previous_menu(void* context) {
    UNUSED(context);
    return TumoSpectrumViewMenu;
}

static uint32_t tumospectrum_text_previous_result(void* context) {
    UNUSED(context);
    return TumoSpectrumViewResult;
}

static uint32_t tumospectrum_text_previous_set(void* context) {
    UNUSED(context);
    return TumoSpectrumViewCaptureSet;
}

static uint32_t tumospectrum_note_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewResult;
}

static uint32_t tumospectrum_set_input_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewMenu;
}

static bool tumospectrum_back(void* context) {
    view_dispatcher_stop(((TumoSpectrumApp*)context)->view_dispatcher);
    return true;
}

static void tumospectrum_show_text(
    TumoSpectrumApp* app,
    const char* title,
    const char* body,
    uint32_t return_view) {
    furi_string_reset(app->text);
    if(title && title[0]) furi_string_cat_printf(app->text, "%s\n\n", title);
    furi_string_cat(app->text, body ? body : "");
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(
        text_box_get_view(app->text_box),
        return_view == TumoSpectrumViewMenu       ? tumospectrum_text_previous_menu :
        return_view == TumoSpectrumViewCaptureSet ? tumospectrum_text_previous_set :
                                                    tumospectrum_text_previous_result);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewText);
}

static TumoSpectrumStatus tumospectrum_parse_selected(
    TumoSpectrumApp* app,
    TumoSpectrumCaptureType requested,
    const char* path,
    TumoSpectrumCapture* destination) {
    if(requested == TumoSpectrumCaptureSubGhzRaw ||
       requested == TumoSpectrumCaptureSubGhzDecoded) {
        return tumospectrum_parse_subghz(app->storage, path, destination);
    }
    if(requested == TumoSpectrumCaptureInfraredRaw ||
       requested == TumoSpectrumCaptureInfraredParsed) {
        return tumospectrum_parse_infrared(app->storage, path, destination);
    }
    if(requested == TumoSpectrumCaptureTumoScope) {
        return tumospectrum_parse_tumoscope(app->storage, path, destination);
    }
    return TumoSpectrumStatusUnsupported;
}

static bool tumospectrum_select_file(
    TumoSpectrumApp* app,
    TumoSpectrumCaptureType requested,
    TumoSpectrumCapture* destination) {
    const char* extension = NULL;
    const char* base_path = NULL;
    const Icon* icon = NULL;
    if(requested == TumoSpectrumCaptureSubGhzRaw ||
       requested == TumoSpectrumCaptureSubGhzDecoded) {
        extension = ".sub";
        base_path = TUMOSPECTRUM_SUBGHZ_DIR;
        icon = &I_sub1_10px;
    } else if(
        requested == TumoSpectrumCaptureInfraredRaw ||
        requested == TumoSpectrumCaptureInfraredParsed) {
        extension = ".ir";
        base_path = TUMOSPECTRUM_INFRARED_DIR;
        icon = &I_ir_10px;
    } else {
        extension = ".vcd";
        base_path = TUMOSPECTRUM_SCOPE_DIR;
        icon = &I_file_10px;
    }

    FuriString* path = furi_string_alloc_set(base_path);
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, extension, icon);
    options.base_path = base_path;
    const bool selected = dialog_file_browser_show(app->dialogs, path, path, &options);
    if(selected) {
        tumospectrum_parse_selected(app, requested, furi_string_get_cstr(path), destination);
    }
    furi_string_free(path);
    return selected;
}

static void tumospectrum_present_capture(TumoSpectrumApp* app) {
    memset(&app->compared, 0, sizeof(app->compared));
    memset(&app->comparison, 0, sizeof(app->comparison));
    app->report_path[0] = '\0';
    app->result_page = 0U;
    tumospectrum_refresh_result(app);
    notification_message(
        app->notification,
        app->capture.status == TumoSpectrumStatusOk ? &tumospectrum_sequence_ok :
                                                      &tumospectrum_sequence_error);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewResult);
}

static void tumospectrum_load_capture_path(
    TumoSpectrumApp* app,
    TumoSpectrumCaptureType requested,
    const char* path) {
    tumospectrum_parse_selected(app, requested, path, &app->capture);
    tumospectrum_present_capture(app);
}

static void tumospectrum_open_capture(TumoSpectrumApp* app, TumoSpectrumCaptureType requested) {
    if(!tumospectrum_select_file(app, requested, &app->capture)) return;
    tumospectrum_present_capture(app);
}

static void tumospectrum_start_capture(TumoSpectrumApp* app, TumoSpectrumCaptureType type) {
    const char* target = type == TumoSpectrumCaptureSubGhzRaw ? "Sub-GHz" : "Infrared";
    FuriString* self_path = furi_string_alloc();
    const bool self_found = loader_get_application_launch_path(app->loader, self_path);
    if(!self_found || !tumospectrum_capture_flow_prepare(app->storage, type)) {
        furi_string_free(self_path);
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Capture",
            self_found ? "Could not prepare the SD capture snapshot." :
                         "Could not resolve the TumoSpectrum app path.",
            TumoSpectrumViewMenu);
        return;
    }

    loader_clear_launch_queue(app->loader);
    loader_enqueue_launch(
        app->loader, target, TUMOSPECTRUM_CAPTURE_LAUNCH_ARG, LoaderDeferredLaunchFlagGui);
    loader_enqueue_launch(
        app->loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    furi_string_free(self_path);
    view_dispatcher_stop(app->view_dispatcher);
}

static void tumospectrum_capture_set_add_sample(TumoSpectrumApp* app) {
    TumoSpectrumCaptureSet* capture_set = &app->capture_set;
    if(capture_set->sample_count >= TUMOSPECTRUM_SET_MAX_SAMPLES) {
        tumospectrum_show_text(
            app,
            "Capture Set",
            "This set already contains the maximum of four captures.",
            TumoSpectrumViewCaptureSet);
        return;
    }

    TumoSpectrumCapture* sample = &capture_set->samples[capture_set->sample_count];
    memset(sample, 0, sizeof(*sample));
    if(!tumospectrum_select_file(app, capture_set->type, sample)) return;
    if(sample->status != TumoSpectrumStatusOk || sample->type != capture_set->type ||
       !tumospectrum_type_has_timings(sample->type)) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "RAW Required",
            "Select a saved RAW capture. Decoded key files do not contain the timing sequence required for inference.",
            TumoSpectrumViewCaptureSet);
        memset(sample, 0, sizeof(*sample));
        return;
    }
    for(size_t index = 0U; index < capture_set->sample_count; index++) {
        if(strcmp(capture_set->samples[index].path, sample->path) == 0) {
            notification_message(app->notification, &tumospectrum_sequence_error);
            tumospectrum_show_text(
                app,
                "Duplicate Capture",
                "Choose a different recording. Reusing one file would produce a misleading stability score.",
                TumoSpectrumViewCaptureSet);
            memset(sample, 0, sizeof(*sample));
            return;
        }
    }

    capture_set->sample_count++;
    capture_set->inferred = false;
    memset(&capture_set->inference, 0, sizeof(capture_set->inference));
    app->set_profile_path[0] = '\0';
    app->set_report_path[0] = '\0';
    tumospectrum_refresh_set(app);
    notification_message(app->notification, &tumospectrum_sequence_ok);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewCaptureSet);
}

static void tumospectrum_capture_set_infer(TumoSpectrumApp* app) {
    TumoSpectrumCaptureSet* capture_set = &app->capture_set;
    capture_set->inference =
        tumospectrum_infer_captures(capture_set->samples, capture_set->sample_count);
    if(!capture_set->inference.compatible) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Inference Failed",
            "The RAW captures are not compatible. Use recordings of the same control at the same frequency and signal family.",
            TumoSpectrumViewCaptureSet);
        return;
    }

    capture_set->inferred = true;
    if(!tumospectrum_storage_save_set(
           app->storage,
           capture_set,
           app->set_profile_path,
           sizeof(app->set_profile_path),
           app->set_report_path,
           sizeof(app->set_report_path))) {
        capture_set->inferred = false;
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Save Failed",
            "Inference completed, but the capture set could not be saved. Check the SD card and try again.",
            TumoSpectrumViewCaptureSet);
        return;
    }

    tumospectrum_refresh_set(app);
    notification_message(app->notification, &tumospectrum_sequence_ok);
}

static void tumospectrum_capture_set_show_details(TumoSpectrumApp* app) {
    tumospectrum_storage_build_set_text(&app->capture_set, app->text);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(text_box_get_view(app->text_box), tumospectrum_text_previous_set);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewText);
}

static void tumospectrum_capture_set_send(TumoSpectrumApp* app) {
    const char* report_path = app->set_report_path[0] ? app->set_report_path :
                                                        TUMOSPECTRUM_LATEST_SET_REPORT;
    const char* filename = strrchr(report_path, '/');
    filename = filename ? filename + 1U : report_path;
    char payload[BT_APP_BRIDGE_V2_PAYLOAD_LEN_MAX + 1U];
    const int length = snprintf(
        payload,
        sizeof(payload),
        "schema=2;file=%s;kind=capture_set;samples=%u;stable=%u",
        filename,
        (unsigned int)app->capture_set.sample_count,
        app->capture_set.inference.stable_percent);
    const bool success =
        length > 0 && (size_t)length < sizeof(payload) &&
        bt_app_bridge_send_text_v2(
            app->bt, TUMOSPECTRUM_BRIDGE_APP_ID, TUMOSPECTRUM_BRIDGE_COMMAND, 0U, 0U, payload);
    notification_message(
        app->notification, success ? &tumospectrum_sequence_ok : &tumospectrum_sequence_error);
    tumospectrum_show_text(
        app,
        "Companion",
        success ? "Capture-set report announced over FAB2." :
                  "The report is saved, but no FAB2 link accepted the announcement.",
        TumoSpectrumViewCaptureSet);
}

static void tumospectrum_set_input_done(void* context) {
    TumoSpectrumApp* app = context;
    if(app->set_input_stage == TumoSpectrumSetInputDevice) {
        strlcpy(
            app->capture_set.device_label,
            app->set_input_buffer[0] ? app->set_input_buffer : "Device",
            sizeof(app->capture_set.device_label));
        app->set_input_stage = TumoSpectrumSetInputControl;
        app->set_input_buffer[0] = '\0';
        text_input_reset(app->set_input);
        text_input_set_header_text(app->set_input, "Control name");
        text_input_set_result_callback(
            app->set_input,
            tumospectrum_set_input_done,
            app,
            app->set_input_buffer,
            sizeof(app->set_input_buffer),
            false);
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewSetInput);
        return;
    }

    strlcpy(
        app->capture_set.control_label,
        app->set_input_buffer[0] ? app->set_input_buffer : "Control",
        sizeof(app->capture_set.control_label));
    tumospectrum_refresh_set(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewCaptureSet);
    tumospectrum_capture_set_add_sample(app);
}

static void tumospectrum_begin_capture_set(TumoSpectrumApp* app, TumoSpectrumCaptureType type) {
    memset(&app->capture_set, 0, sizeof(app->capture_set));
    app->capture_set.type = type;
    app->set_profile_path[0] = '\0';
    app->set_report_path[0] = '\0';
    app->set_input_stage = TumoSpectrumSetInputDevice;
    app->set_input_buffer[0] = '\0';
    text_input_reset(app->set_input);
    text_input_set_header_text(app->set_input, "Device name");
    text_input_set_result_callback(
        app->set_input,
        tumospectrum_set_input_done,
        app,
        app->set_input_buffer,
        sizeof(app->set_input_buffer),
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewSetInput);
}

static void tumospectrum_open_latest_set(TumoSpectrumApp* app) {
    if(!tumospectrum_storage_load_latest_set(app->storage, &app->capture_set)) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app, "Latest Set", "No valid capture set is available yet.", TumoSpectrumViewMenu);
        return;
    }
    strlcpy(app->set_profile_path, TUMOSPECTRUM_LATEST_SET, sizeof(app->set_profile_path));
    strlcpy(app->set_report_path, TUMOSPECTRUM_LATEST_SET_REPORT, sizeof(app->set_report_path));
    tumospectrum_refresh_set(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewCaptureSet);
}

static bool tumospectrum_resume_capture(TumoSpectrumApp* app) {
    TumoSpectrumCaptureType type = TumoSpectrumCaptureNone;
    char path[TUMOSPECTRUM_PATH_SIZE];
    bool needs_selection = false;
    if(!tumospectrum_capture_flow_resume(
           app->storage, &type, path, sizeof(path), &needs_selection)) {
        return false;
    }
    if(path[0]) {
        tumospectrum_load_capture_path(app, type, path);
    } else if(needs_selection) {
        tumospectrum_open_capture(app, type);
    } else {
        tumospectrum_show_text(
            app,
            "Capture",
            "No new saved capture was found. The original files were not changed.",
            TumoSpectrumViewMenu);
    }
    return true;
}

static void tumospectrum_menu_callback(void* context, uint32_t index) {
    TumoSpectrumApp* app = context;
    switch(index) {
    case TumoSpectrumMenuCaptureSubGhz:
        tumospectrum_start_capture(app, TumoSpectrumCaptureSubGhzRaw);
        break;
    case TumoSpectrumMenuCaptureInfrared:
        tumospectrum_start_capture(app, TumoSpectrumCaptureInfraredRaw);
        break;
    case TumoSpectrumMenuOpenSubGhz:
        tumospectrum_open_capture(app, TumoSpectrumCaptureSubGhzRaw);
        break;
    case TumoSpectrumMenuOpenInfrared:
        tumospectrum_open_capture(app, TumoSpectrumCaptureInfraredRaw);
        break;
    case TumoSpectrumMenuNewSubGhzSet:
        tumospectrum_begin_capture_set(app, TumoSpectrumCaptureSubGhzRaw);
        break;
    case TumoSpectrumMenuNewInfraredSet:
        tumospectrum_begin_capture_set(app, TumoSpectrumCaptureInfraredRaw);
        break;
    case TumoSpectrumMenuLatestSet:
        tumospectrum_open_latest_set(app);
        break;
    case TumoSpectrumMenuOpenScope:
        tumospectrum_open_capture(app, TumoSpectrumCaptureTumoScope);
        break;
    case TumoSpectrumMenuImportAnalyzer:
        tumospectrum_import_frequency_observation(app->storage, &app->capture);
        memset(&app->comparison, 0, sizeof(app->comparison));
        app->report_path[0] = '\0';
        app->result_page = 0U;
        tumospectrum_refresh_result(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewResult);
        break;
    case TumoSpectrumMenuNotebook:
        furi_string_reset(app->text);
        if(tumospectrum_storage_load_latest(app->storage, app->text)) {
            text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
            text_box_set_focus(app->text_box, TextBoxFocusStart);
            view_set_previous_callback(
                text_box_get_view(app->text_box), tumospectrum_text_previous_menu);
            view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewText);
        } else {
            tumospectrum_show_text(
                app,
                "Notebook",
                "No saved observations.\n\nOpen a capture, choose Actions, add a note and save a report.",
                TumoSpectrumViewMenu);
        }
        break;
    case TumoSpectrumMenuAbout:
        tumospectrum_show_text(
            app,
            "TumoSpectrum 2.0",
            "Live capture and multi-capture signal research workspace.\n\n"
            "Capture sets compare three or four RAW recordings to identify timing clusters and stable or changing regions.\n\n"
            "Source files are never modified. Static-like is not a cryptographic or replay guarantee. Replay stays in stock apps.\n\n"
            "github.com/squazaryu/tumoflip\nIssue #123",
            TumoSpectrumViewMenu);
        break;
    default:
        break;
    }
}

static bool tumospectrum_save_report(TumoSpectrumApp* app) {
    const bool success = tumospectrum_storage_save(
        app->storage,
        &app->capture,
        app->comparison.compatible ? &app->compared : NULL,
        app->comparison.compatible ? &app->comparison : NULL,
        app->report_path,
        sizeof(app->report_path));
    notification_message(
        app->notification, success ? &tumospectrum_sequence_ok : &tumospectrum_sequence_error);
    return success;
}

static void tumospectrum_note_done(void* context) {
    TumoSpectrumApp* app = context;
    strlcpy(app->capture.note, app->note_buffer, sizeof(app->capture.note));
    app->report_path[0] = '\0';
    tumospectrum_refresh_result(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewResult);
}

static void tumospectrum_begin_note(TumoSpectrumApp* app) {
    strlcpy(app->note_buffer, app->capture.note, sizeof(app->note_buffer));
    text_input_reset(app->note_input);
    text_input_set_header_text(app->note_input, "Observation note");
    text_input_set_result_callback(
        app->note_input,
        tumospectrum_note_done,
        app,
        app->note_buffer,
        sizeof(app->note_buffer),
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewNote);
}

static void tumospectrum_compare_capture(TumoSpectrumApp* app) {
    if(!tumospectrum_select_file(app, app->capture.type, &app->compared)) return;
    app->comparison = tumospectrum_compare(&app->capture, &app->compared);
    app->report_path[0] = '\0';
    if(!app->comparison.compatible) {
        tumospectrum_show_text(
            app,
            "Compare",
            "Captures are not compatible.\n\nUse two RAW captures from the same signal family.",
            TumoSpectrumViewResult);
        notification_message(app->notification, &tumospectrum_sequence_error);
        return;
    }
    app->result_page = 3U;
    tumospectrum_refresh_result(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewResult);
}

static void tumospectrum_handoff(TumoSpectrumApp* app) {
    const char* target = NULL;
    if(app->capture.type == TumoSpectrumCaptureSubGhzRaw ||
       app->capture.type == TumoSpectrumCaptureSubGhzDecoded) {
        target = "Sub-GHz";
    } else if(
        app->capture.type == TumoSpectrumCaptureInfraredRaw ||
        app->capture.type == TumoSpectrumCaptureInfraredParsed) {
        target = "Infrared";
    }
    if(!target) return;

    loader_clear_launch_queue(app->loader);
    loader_enqueue_launch(app->loader, target, app->capture.path, LoaderDeferredLaunchFlagGui);
    FuriString* self_path = furi_string_alloc();
    if(loader_get_application_launch_path(app->loader, self_path)) {
        loader_enqueue_launch(
            app->loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    }
    furi_string_free(self_path);
    view_dispatcher_stop(app->view_dispatcher);
}

static bool tumospectrum_send_companion(TumoSpectrumApp* app) {
    if(!app->report_path[0] && !tumospectrum_save_report(app)) return false;
    const char* filename = strrchr(app->report_path, '/');
    filename = filename ? filename + 1U : app->report_path;
    char payload[BT_APP_BRIDGE_V2_PAYLOAD_LEN_MAX + 1U];
    const int length = snprintf(
        payload,
        sizeof(payload),
        "schema=1;file=%s;type=%s;freq=%lu;sim=%u",
        filename,
        tumospectrum_badge(app->capture.type),
        (unsigned long)app->capture.frequency_hz,
        app->comparison.compatible ? app->comparison.overall_similarity : 0U);
    return length > 0 && (size_t)length < sizeof(payload) &&
           bt_app_bridge_send_text_v2(
               app->bt, TUMOSPECTRUM_BRIDGE_APP_ID, TUMOSPECTRUM_BRIDGE_COMMAND, 0U, 0U, payload);
}

static void tumospectrum_action_callback(void* context, uint32_t index) {
    TumoSpectrumApp* app = context;
    switch(index) {
    case TumoSpectrumActionNote:
        tumospectrum_begin_note(app);
        break;
    case TumoSpectrumActionSave: {
        const bool success = tumospectrum_save_report(app);
        char body[256];
        snprintf(
            body,
            sizeof(body),
            "%s\n\n%s",
            success ? "Report and notebook saved." : "Could not save report.",
            success ? app->report_path : "Check SD card.");
        tumospectrum_show_text(app, "Save Report", body, TumoSpectrumViewResult);
        break;
    }
    case TumoSpectrumActionCompare:
        tumospectrum_compare_capture(app);
        break;
    case TumoSpectrumActionHandoff:
        tumospectrum_handoff(app);
        break;
    case TumoSpectrumActionCompanion: {
        const bool success = tumospectrum_send_companion(app);
        notification_message(
            app->notification, success ? &tumospectrum_sequence_ok : &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Companion",
            success ?
                "Report announced over FAB2.\n\nOpen TumoSpectrum in Companion to fetch and visualize the saved JSON report." :
                "Report is saved, but no FAB2 link accepted the announcement.",
            TumoSpectrumViewResult);
        break;
    }
    case TumoSpectrumActionDetails:
        tumospectrum_storage_build_text(
            &app->capture,
            app->comparison.compatible ? &app->compared : NULL,
            app->comparison.compatible ? &app->comparison : NULL,
            app->text);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
        text_box_set_focus(app->text_box, TextBoxFocusStart);
        view_set_previous_callback(
            text_box_get_view(app->text_box), tumospectrum_text_previous_result);
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewText);
        break;
    default:
        break;
    }
}

static void tumospectrum_build_actions(TumoSpectrumApp* app) {
    submenu_reset(app->actions);
    submenu_set_header(app->actions, "TumoSpectrum Actions");
    submenu_add_item(
        app->actions,
        app->capture.note[0] ? "Edit Note" : "Add Note",
        TumoSpectrumActionNote,
        tumospectrum_action_callback,
        app);
    submenu_add_item(
        app->actions, "Save Report", TumoSpectrumActionSave, tumospectrum_action_callback, app);
    if(tumospectrum_type_has_timings(app->capture.type)) {
        submenu_add_item(
            app->actions,
            "Compare Capture",
            TumoSpectrumActionCompare,
            tumospectrum_action_callback,
            app);
    }
    if(tumospectrum_type_can_handoff(app->capture.type)) {
        submenu_add_item(
            app->actions,
            app->capture.type == TumoSpectrumCaptureSubGhzRaw ||
                    app->capture.type == TumoSpectrumCaptureSubGhzDecoded ?
                "Open in Sub-GHz" :
                "Open in Infrared",
            TumoSpectrumActionHandoff,
            tumospectrum_action_callback,
            app);
    }
    submenu_add_item(
        app->actions,
        "Send to Companion",
        TumoSpectrumActionCompanion,
        tumospectrum_action_callback,
        app);
    submenu_add_item(
        app->actions, "Full Details", TumoSpectrumActionDetails, tumospectrum_action_callback, app);
}

static TumoSpectrumApp* tumospectrum_alloc(void) {
    TumoSpectrumApp* app = malloc(sizeof(*app));
    if(!app) return NULL;
    memset(app, 0, sizeof(*app));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->loader = furi_record_open(RECORD_LOADER);
    app->bt = furi_record_open(RECORD_BT);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->actions = submenu_alloc();
    app->result_view = view_alloc();
    app->capture_set_view = view_alloc();
    app->text_box = text_box_alloc();
    app->note_input = text_input_alloc();
    app->set_input = text_input_alloc();
    app->text = furi_string_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, tumospectrum_back);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    submenu_set_header(app->menu, "TumoSpectrum");
    submenu_add_item(
        app->menu,
        "Capture Sub-GHz RAW",
        TumoSpectrumMenuCaptureSubGhz,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "Capture Infrared RAW",
        TumoSpectrumMenuCaptureInfrared,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "Open Saved Sub-GHz",
        TumoSpectrumMenuOpenSubGhz,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "Open Saved Infrared",
        TumoSpectrumMenuOpenInfrared,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "New Sub-GHz Set",
        TumoSpectrumMenuNewSubGhzSet,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "New Infrared Set",
        TumoSpectrumMenuNewInfraredSet,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "Latest Capture Set",
        TumoSpectrumMenuLatestSet,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu, "Open TumoScope", TumoSpectrumMenuOpenScope, tumospectrum_menu_callback, app);
    submenu_add_item(
        app->menu,
        "Import Analyzer",
        TumoSpectrumMenuImportAnalyzer,
        tumospectrum_menu_callback,
        app);
    submenu_add_item(
        app->menu, "Notebook", TumoSpectrumMenuNotebook, tumospectrum_menu_callback, app);
    submenu_add_item(app->menu, "About", TumoSpectrumMenuAbout, tumospectrum_menu_callback, app);

    view_allocate_model(app->result_view, ViewModelTypeLocking, sizeof(TumoSpectrumResultModel));
    view_set_context(app->result_view, app);
    view_set_draw_callback(app->result_view, tumospectrum_result_draw);
    view_set_input_callback(app->result_view, tumospectrum_result_input);
    view_set_previous_callback(app->result_view, tumospectrum_result_previous);
    view_allocate_model(app->capture_set_view, ViewModelTypeLocking, sizeof(TumoSpectrumSetModel));
    view_set_context(app->capture_set_view, app);
    view_set_draw_callback(app->capture_set_view, tumospectrum_capture_set_draw);
    view_set_input_callback(app->capture_set_view, tumospectrum_capture_set_input);
    view_set_previous_callback(app->capture_set_view, tumospectrum_text_previous_menu);
    view_set_previous_callback(submenu_get_view(app->actions), tumospectrum_actions_previous);

    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(text_box_get_view(app->text_box), tumospectrum_text_previous_menu);
    view_set_previous_callback(text_input_get_view(app->note_input), tumospectrum_note_previous);
    view_set_previous_callback(
        text_input_get_view(app->set_input), tumospectrum_set_input_previous);

    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->view_dispatcher, TumoSpectrumViewResult, app->result_view);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewActions, submenu_get_view(app->actions));
    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewText, text_box_get_view(app->text_box));
    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewNote, text_input_get_view(app->note_input));
    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewCaptureSet, app->capture_set_view);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewSetInput, text_input_get_view(app->set_input));
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewMenu);
    tumospectrum_storage_prepare(app->storage);
    return app;
}

static void tumospectrum_free(TumoSpectrumApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewSetInput);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewCaptureSet);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewNote);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewText);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewActions);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewResult);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewMenu);
    text_input_free(app->set_input);
    text_input_free(app->note_input);
    text_box_free(app->text_box);
    view_free(app->capture_set_view);
    view_free(app->result_view);
    submenu_free(app->actions);
    submenu_free(app->menu);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->text);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t signal_workbench_app(void* context) {
    UNUSED(context);
    TumoSpectrumApp* app = tumospectrum_alloc();
    if(!app) return -1;
    tumospectrum_resume_capture(app);
    view_dispatcher_run(app->view_dispatcher);
    tumospectrum_free(app);
    return 0;
}
