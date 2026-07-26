#include "tumospectrum_analysis.h"
#include "tumospectrum_band_map.h"
#include "tumospectrum_band_session.h"
#include "tumospectrum_capture_flow.h"
#include "tumospectrum_inference.h"
#include "tumospectrum_parser.h"
#include "tumospectrum_profile_builder.h"
#include "tumospectrum_protocol_runtime.h"
#include "tumospectrum_storage.h"
#include "signal_workbench_icons.h"

#include <bt/bt_service/bt.h>
#include <dialogs/dialogs.h>
#include <furi.h>
#include <furi_hal_info.h>
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

#define TUMOSPECTRUM_SUBGHZ_DIR      EXT_PATH("subghz")
#define TUMOSPECTRUM_INFRARED_DIR    EXT_PATH("infrared")
#define TUMOSPECTRUM_SCOPE_DIR       EXT_PATH("apps_data/tumoscope/captures")
#define TUMOSPECTRUM_BRIDGE_APP_ID   "signal_workbench"
#define TUMOSPECTRUM_BRIDGE_COMMAND  "report"
#define TUMOSPECTRUM_LATEST_SET_ARG  "tumospectrum_latest_set"
#define TUMOSPECTRUM_PROFILES_ARG    "tumospectrum_profiles"
#define TUMOSPECTRUM_DEMO_PROFILE_ID "035084cd0da9ab62"

typedef enum {
    TumoSpectrumViewMenu,
    TumoSpectrumViewBandMap,
    TumoSpectrumViewResult,
    TumoSpectrumViewActions,
    TumoSpectrumViewText,
    TumoSpectrumViewNote,
    TumoSpectrumViewCaptureSet,
    TumoSpectrumViewSetActions,
    TumoSpectrumViewSetInput,
    TumoSpectrumViewProfileName,
    TumoSpectrumViewProfileMenu,
    TumoSpectrumViewProtocol,
} TumoSpectrumView;

typedef enum {
    TumoSpectrumMenuBandMap,
    TumoSpectrumMenuCaptureSubGhz,
    TumoSpectrumMenuCaptureInfrared,
    TumoSpectrumMenuOpenSubGhz,
    TumoSpectrumMenuOpenInfrared,
    TumoSpectrumMenuNewSubGhzSet,
    TumoSpectrumMenuNewInfraredSet,
    TumoSpectrumMenuLatestSet,
    TumoSpectrumMenuOpenScope,
    TumoSpectrumMenuImportAnalyzer,
    TumoSpectrumMenuProtocolProfiles,
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
    TumoSpectrumSetActionCreateProfile,
    TumoSpectrumSetActionCompanion,
    TumoSpectrumSetActionHandoff,
    TumoSpectrumSetActionDetails,
    TumoSpectrumSetActionClearSession,
} TumoSpectrumSetAction;

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

typedef struct {
    TumoSpectrumApp* app;
} TumoSpectrumProtocolModel;

typedef struct {
    TumoSpectrumApp* app;
} TumoSpectrumBandMapModel;

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
    Submenu* set_actions;
    Submenu* profile_menu;
    View* result_view;
    View* capture_set_view;
    View* protocol_view;
    View* band_map_view;
    TextBox* text_box;
    TextInput* note_input;
    TextInput* set_input;
    TextInput* profile_name_input;
    FuriString* text;
    TumoSpectrumCapture capture;
    TumoSpectrumCapture compared;
    TumoSpectrumComparison comparison;
    char note_buffer[TUMOSPECTRUM_NOTE_SIZE];
    char report_path[TUMOSPECTRUM_PATH_SIZE];
    TumoSpectrumCaptureSet capture_set;
    TumoSpectrumSetInputStage set_input_stage;
    char set_input_buffer[TUMOSPECTRUM_SET_LABEL_SIZE];
    char profile_name_buffer[ProtocolProfileNameSize];
    char set_profile_path[TUMOSPECTRUM_PATH_SIZE];
    char set_report_path[TUMOSPECTRUM_PATH_SIZE];
    TumoSpectrumProtocolRuntime* protocol_runtime;
    TumoSpectrumBandMap* band_map;
    ProtocolProfilePackage protocol_packages[ProtocolProfileMaximumProfiles];
    TumoSpectrumProtocolObservation protocol_observation;
    size_t protocol_package_count;
    size_t selected_protocol_package;
    uint32_t current_api;
    ProtocolProfileStatus protocol_result_status;
    bool has_protocol_observation;
    bool protocol_demo_result;
    bool smart_capture_set;
    uint32_t smart_capture_frequency_hz;
    uint8_t smart_capture_sample_count;
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

static void tumospectrum_refresh_protocol(TumoSpectrumApp* app) {
    with_view_model(
        app->protocol_view, TumoSpectrumProtocolModel * model, { model->app = app; }, true);
}

static void tumospectrum_refresh_band_map(TumoSpectrumApp* app) {
    with_view_model(
        app->band_map_view, TumoSpectrumBandMapModel * model, { model->app = app; }, true);
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
static void tumospectrum_start_smart_capture(TumoSpectrumApp* app, uint32_t frequency_hz);

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

static uint8_t tumospectrum_band_map_rssi_y(int8_t rssi) {
    const int16_t clamped = rssi < -110 ? -110 : rssi > -35 ? -35 : rssi;
    return (uint8_t)(40 - ((clamped + 110) * 20) / 75);
}

static const char* tumospectrum_band_map_status_name(TumoSpectrumBandMapStatus status) {
    switch(status) {
    case TumoSpectrumBandMapStatusScanning:
        return "RUN";
    case TumoSpectrumBandMapStatusHold:
        return "HOLD";
    case TumoSpectrumBandMapStatusBusy:
        return "BUSY";
    case TumoSpectrumBandMapStatusNoRadio:
        return "NO RF";
    case TumoSpectrumBandMapStatusError:
        return "ERROR";
    case TumoSpectrumBandMapStatusIdle:
    default:
        return "IDLE";
    }
}

static void tumospectrum_band_map_draw(Canvas* canvas, void* context) {
    const TumoSpectrumBandMapModel* model = context;
    const TumoSpectrumApp* app = model->app;
    TumoSpectrumBandMapSnapshot snapshot;
    tumospectrum_band_map_get_snapshot(app->band_map, &snapshot);
    const TumoSpectrumBandMapBand* band = tumospectrum_band_map_band(snapshot.band_index);
    char text[32];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    snprintf(text, sizeof(text), "%s", band ? band->name : "---");
    canvas_draw_str(canvas, 1, 8, text);
    snprintf(
        text,
        sizeof(text),
        "%03lu.%03lu",
        (unsigned long)(snapshot.selected_frequency_hz / 1000000U),
        (unsigned long)((snapshot.selected_frequency_hz % 1000000U) / 1000U));
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, text);
    snprintf(
        text,
        sizeof(text),
        "%s",
        snapshot.radio == TumoSpectrumBandMapRadioExternal ? "EXT" : "INT");
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, text);
    canvas_draw_line(canvas, 0, 10, 127, 10);

    snprintf(
        text,
        sizeof(text),
        "Z%u %s",
        snapshot.zoom,
        tumospectrum_band_map_status_name(snapshot.status));
    canvas_draw_str(canvas, 1, 18, text);
    snprintf(text, sizeof(text), "R%d N%d", snapshot.selected_rssi_dbm, snapshot.noise_floor_dbm);
    canvas_draw_str_aligned(canvas, 72, 18, AlignCenter, AlignBottom, text);
    snprintf(text, sizeof(text), "%u/4", app->smart_capture_sample_count);
    canvas_draw_str_aligned(canvas, 127, 18, AlignRight, AlignBottom, text);

    const uint8_t noise_y = tumospectrum_band_map_rssi_y(snapshot.noise_floor_dbm);
    for(uint8_t x = 0U; x < 128U; x += 4U) {
        canvas_draw_dot(canvas, x, noise_y);
    }
    for(uint8_t bin = 0U; bin < TUMOSPECTRUM_BAND_MAP_BINS; bin++) {
        const uint8_t x = (uint8_t)(bin * 2U);
        const uint8_t y = tumospectrum_band_map_rssi_y(snapshot.bins[bin]);
        canvas_draw_line(canvas, x, 40, x, y);
        const uint8_t peak_y = tumospectrum_band_map_rssi_y(snapshot.peaks[bin]);
        canvas_draw_dot(canvas, x, peak_y);
    }

    const uint8_t cursor_x = (uint8_t)(snapshot.selected_bin * 2U);
    canvas_draw_line(canvas, cursor_x, 19, cursor_x, 41);
    canvas_draw_triangle(canvas, cursor_x, 19, 2, 2, CanvasDirectionBottomToTop);

    for(uint8_t row = 0U; row < TUMOSPECTRUM_BAND_MAP_HISTORY_ROWS; row++) {
        for(uint8_t bin = 0U; bin < TUMOSPECTRUM_BAND_MAP_BINS; bin++) {
            const int16_t above_floor =
                (int16_t)snapshot.history[row][bin] - snapshot.noise_floor_dbm;
            if(above_floor > 7) {
                const uint8_t x = (uint8_t)(bin * 2U);
                const uint8_t y = (uint8_t)(42U + row);
                canvas_draw_dot(canvas, x, y);
                if(above_floor > 15 && x < 127U) canvas_draw_dot(canvas, x + 1U, y);
            }
        }
    }
    canvas_draw_line(canvas, 0, 51, 127, 51);

    elements_button_left(canvas, "Tune");
    elements_button_center(canvas, "Capture");
    elements_button_right(canvas, "Tune");
}

static bool tumospectrum_band_map_input(InputEvent* event, void* context) {
    TumoSpectrumApp* app = context;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyLeft || event->key == InputKeyRight) {
            tumospectrum_band_map_move_cursor(app->band_map, event->key == InputKeyLeft ? -1 : 1);
        } else if(event->type == InputTypeRepeat) {
            return false;
        } else if(event->key == InputKeyUp) {
            TumoSpectrumBandMapSnapshot snapshot;
            tumospectrum_band_map_get_snapshot(app->band_map, &snapshot);
            tumospectrum_band_map_set_band(
                app->band_map, (snapshot.band_index + 1U) % tumospectrum_band_map_band_count());
        } else if(event->key == InputKeyDown) {
            tumospectrum_band_map_cycle_radio(app->band_map);
        } else if(event->key == InputKeyOk) {
            TumoSpectrumBandMapSnapshot snapshot;
            tumospectrum_band_map_get_snapshot(app->band_map, &snapshot);
            if(snapshot.selected_frequency_hz != 0U) {
                tumospectrum_start_smart_capture(app, snapshot.selected_frequency_hz);
                return true;
            }
        } else if(event->key == InputKeyBack) {
            tumospectrum_band_map_stop(app->band_map);
            view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewMenu);
            return true;
        } else {
            return false;
        }
        tumospectrum_refresh_band_map(app);
        return true;
    }
    if(event->type == InputTypeLong) {
        if(event->key == InputKeyOk) {
            tumospectrum_band_map_toggle_hold(app->band_map);
        } else if(event->key == InputKeyUp) {
            tumospectrum_band_map_cycle_zoom(app->band_map);
        } else if(event->key == InputKeyDown) {
            tumospectrum_band_map_snap_to_peak(app->band_map);
        } else {
            return false;
        }
        tumospectrum_refresh_band_map(app);
        return true;
    }
    return false;
}

static void tumospectrum_capture_set_add_sample(TumoSpectrumApp* app);
static void tumospectrum_capture_set_infer(TumoSpectrumApp* app);
static void tumospectrum_capture_set_show_details(TumoSpectrumApp* app);
static void tumospectrum_capture_set_send(TumoSpectrumApp* app);
static void tumospectrum_capture_set_build_actions(TumoSpectrumApp* app);
static void tumospectrum_capture_set_begin_profile(TumoSpectrumApp* app);
static void tumospectrum_open_protocol_profiles(TumoSpectrumApp* app);
static void tumospectrum_handoff_capture(
    TumoSpectrumApp* app,
    const TumoSpectrumCapture* capture,
    const char* return_argument);

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
            "%u bits  S%u C%u ?%u",
            capture_set->inference.bit_count,
            capture_set->inference.stable_bits,
            capture_set->inference.changing_bits,
            capture_set->inference.unknown_bits);
        canvas_draw_str(canvas, 2, 45, line);
        canvas_draw_str(
            canvas, 2, 53, tumospectrum_replay_class_name(capture_set->inference.replay_class));
        elements_button_center(canvas, "Details");
        elements_button_right(canvas, "More");
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
        elements_button_center(canvas, app->smart_capture_set ? "Capture" : "Add");
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
        } else if(app->smart_capture_set && app->smart_capture_frequency_hz != 0U) {
            tumospectrum_start_smart_capture(app, app->smart_capture_frequency_hz);
        } else {
            tumospectrum_capture_set_add_sample(app);
        }
        return true;
    }
    if(event->key == InputKeyRight) {
        if(app->capture_set.inferred) {
            tumospectrum_capture_set_build_actions(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewSetActions);
        } else if(app->capture_set.sample_count >= TUMOSPECTRUM_SET_MIN_SAMPLES) {
            tumospectrum_capture_set_infer(app);
        }
        return true;
    }
    return false;
}

static const ProtocolProfilePackage* tumospectrum_selected_protocol(const TumoSpectrumApp* app) {
    if(app->selected_protocol_package >= app->protocol_package_count) return NULL;
    return &app->protocol_packages[app->selected_protocol_package];
}

static bool tumospectrum_protocol_has_demo(const ProtocolProfilePackage* package) {
    return package != NULL && (strcmp(package->profile_id, TUMOSPECTRUM_DEMO_PROFILE_ID) == 0 ||
                               strcmp(package->filename, PROTOCOL_PROFILE_DEMO_FILENAME) == 0);
}

static const char* tumospectrum_protocol_status_short(ProtocolProfileStatus status) {
    switch(status) {
    case ProtocolProfileStatusOk:
        return "Match";
    case ProtocolProfileStatusUnsupportedVersion:
        return "Bad version";
    case ProtocolProfileStatusApiMismatch:
        return "API mismatch";
    case ProtocolProfileStatusUnsafeProfile:
        return "Unsafe profile";
    case ProtocolProfileStatusUnsupportedCapture:
        return "RAW only";
    case ProtocolProfileStatusNotReceiveOnly:
        return "RX gate failed";
    case ProtocolProfileStatusStableMismatch:
        return "Stable mismatch";
    case ProtocolProfileStatusChecksumMismatch:
        return "Checksum fail";
    case ProtocolProfileStatusNoMatch:
        return "No match";
    case ProtocolProfileStatusInvalidArgument:
    default:
        return "Invalid input";
    }
}

static void tumospectrum_protocol_observation_callback(
    const TumoSpectrumProtocolObservation* observation,
    void* context) {
    TumoSpectrumApp* app = context;
    app->protocol_observation = *observation;
    app->protocol_result_status = ProtocolProfileStatusOk;
    app->has_protocol_observation = true;
    app->protocol_demo_result = false;
    const ProtocolProfilePackage* package = tumospectrum_selected_protocol(app);
    if(package != NULL && (observation->match_count == 1U || observation->changed_mask != 0U)) {
        protocol_profile_observation_append(
            app->storage,
            package,
            &observation->decode,
            observation->changed_mask,
            observation->match_count);
    }
    tumospectrum_refresh_protocol(app);
}

static void tumospectrum_protocol_draw(Canvas* canvas, void* context) {
    const TumoSpectrumProtocolModel* model = context;
    const TumoSpectrumApp* app = model->app;
    const ProtocolProfilePackage* package = tumospectrum_selected_protocol(app);
    const bool listening = tumospectrum_protocol_runtime_is_listening(app->protocol_runtime);
    const int32_t rssi_dbm = (int32_t)tumospectrum_protocol_runtime_rssi(app->protocol_runtime);
    char line[64];
    char badge[8];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Protocol RX");
    const char* badge_text = "READY";
    if(listening && app->has_protocol_observation) {
        const uint32_t visible_count = app->protocol_observation.match_count > 999U ?
                                           999U :
                                           app->protocol_observation.match_count;
        snprintf(badge, sizeof(badge), "L#%lu", (unsigned long)visible_count);
        badge_text = badge;
    } else if(listening) {
        badge_text = "LIVE";
    } else if(app->protocol_demo_result) {
        badge_text = "DEMO";
    }
    tumospectrum_draw_badge(canvas, badge_text);
    canvas_draw_line(canvas, 0, 15, 127, 15);
    canvas_set_font(canvas, FontSecondary);

    if(package == NULL) {
        canvas_draw_str(canvas, 2, 29, "No .tproto profiles");
        canvas_draw_str(canvas, 2, 41, "Install FW Packages");
        elements_button_left(canvas, "Back");
        return;
    }
    snprintf(line, sizeof(line), "%.20s", package->name[0] ? package->name : package->filename);
    canvas_draw_str(canvas, 2, 23, line);
    if(package->status != ProtocolProfileStatusOk) {
        canvas_draw_str(canvas, 2, 36, protocol_profile_status_name(package->status));
        canvas_draw_str(canvas, 2, 47, "Check profile / API");
        elements_button_left(canvas, "Back");
        if(!tumospectrum_protocol_has_demo(package)) elements_button_right(canvas, "Delete");
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "%lu.%03luM %ub %lddB",
        (unsigned long)(package->profile.frequency_hz / 1000000U),
        (unsigned long)((package->profile.frequency_hz / 1000U) % 1000U),
        package->profile.bit_count,
        (long)rssi_dbm);
    canvas_draw_str(canvas, 2, 32, line);
    if(app->has_protocol_observation) {
        const uint8_t digits = (uint8_t)((app->protocol_observation.decode.bit_count + 3U) / 4U);
        snprintf(
            line,
            sizeof(line),
            "V 0x%0*llX",
            digits,
            (unsigned long long)app->protocol_observation.decode.value);
        canvas_draw_str(canvas, 2, 41, line);
        snprintf(
            line,
            sizeof(line),
            "D 0x%0*llX",
            digits,
            (unsigned long long)app->protocol_observation.changed_mask);
        canvas_draw_str(canvas, 2, 50, line);
    } else {
        const ProtocolProfileStatus decode_status =
            listening ? tumospectrum_protocol_runtime_decode_status(app->protocol_runtime) :
                        app->protocol_result_status;
        canvas_draw_str(
            canvas,
            2,
            41,
            listening ? "Waiting for match..." :
                        tumospectrum_protocol_radio_status_name(
                            tumospectrum_protocol_runtime_status(app->protocol_runtime)));
        snprintf(
            line,
            sizeof(line),
            "%s P%u",
            tumospectrum_protocol_status_short(decode_status),
            tumospectrum_protocol_runtime_pulse_count(app->protocol_runtime));
        canvas_draw_str(canvas, 2, 50, line);
    }
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, listening ? "Stop" : "Start");
    if(!listening) {
        elements_button_right(canvas, tumospectrum_protocol_has_demo(package) ? "Demo" : "Delete");
    }
}

static void tumospectrum_protocol_delete(TumoSpectrumApp* app) {
    const ProtocolProfilePackage* package = tumospectrum_selected_protocol(app);
    if(package == NULL || tumospectrum_protocol_has_demo(package)) return;

    char prompt[64];
    snprintf(
        prompt,
        sizeof(prompt),
        "Delete %.18s?",
        package->name[0] ? package->name : package->filename);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Delete profile?", 64, 4, AlignCenter, AlignTop);
    dialog_message_set_text(message, prompt, 64, 26, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Delete", NULL, "Keep");
    const DialogMessageButton result = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
    if(result != DialogMessageButtonLeft) return;

    if(protocol_profile_package_delete(app->storage, package)) {
        notification_message(app->notification, &tumospectrum_sequence_ok);
        tumospectrum_open_protocol_profiles(app);
    } else {
        notification_message(app->notification, &tumospectrum_sequence_error);
        dialog_message_show_storage_error(app->dialogs, "Cannot delete\nprofile");
    }
}

static void tumospectrum_protocol_demo(TumoSpectrumApp* app) {
    const ProtocolProfilePackage* package = tumospectrum_selected_protocol(app);
    if(package == NULL || package->status != ProtocolProfileStatusOk ||
       !tumospectrum_protocol_has_demo(package)) {
        return;
    }

    int32_t* pulses = malloc(sizeof(int32_t) * ProtocolProfileMaximumCapturePulses);
    if(pulses == NULL) {
        app->protocol_result_status = ProtocolProfileStatusInvalidArgument;
        app->has_protocol_observation = false;
        app->protocol_demo_result = true;
        tumospectrum_refresh_protocol(app);
        return;
    }
    ProtocolProfileCaptureInfo capture_info = {0};
    ProtocolProfileDecodeResult decode = {0};
    ProtocolProfileStatus status = protocol_profile_capture_load(
        app->storage,
        PROTOCOL_PROFILE_DEMO_CAPTURE,
        pulses,
        ProtocolProfileMaximumCapturePulses,
        &capture_info);
    if(status == ProtocolProfileStatusOk && package->profile.frequency_hz > 0U &&
       capture_info.frequency_hz != package->profile.frequency_hz) {
        status = ProtocolProfileStatusNoMatch;
    } else if(status == ProtocolProfileStatusOk) {
        status = protocol_profile_decode(
            &package->profile, app->current_api, pulses, capture_info.pulse_count, &decode);
    }
    free(pulses);
    app->protocol_result_status = status;
    app->protocol_demo_result = true;
    app->has_protocol_observation = status == ProtocolProfileStatusOk;
    if(app->has_protocol_observation) {
        app->protocol_observation = (TumoSpectrumProtocolObservation){
            .decode = decode,
            .changed_mask = 0U,
            .match_count = 1U,
        };
    }
    notification_message(
        app->notification,
        status == ProtocolProfileStatusOk ? &tumospectrum_sequence_ok :
                                            &tumospectrum_sequence_error);
    tumospectrum_refresh_protocol(app);
}

static bool tumospectrum_protocol_input(InputEvent* event, void* context) {
    TumoSpectrumApp* app = context;
    if(event->type != InputTypeShort) return false;
    const bool listening = tumospectrum_protocol_runtime_is_listening(app->protocol_runtime);
    if(event->key == InputKeyBack || event->key == InputKeyLeft) {
        tumospectrum_protocol_runtime_stop(app->protocol_runtime);
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewProfileMenu);
        return true;
    }
    if(event->key == InputKeyOk) {
        if(listening) {
            tumospectrum_protocol_runtime_stop(app->protocol_runtime);
        } else {
            const ProtocolProfilePackage* package = tumospectrum_selected_protocol(app);
            app->has_protocol_observation = false;
            app->protocol_demo_result = false;
            app->protocol_result_status = ProtocolProfileStatusNoMatch;
            tumospectrum_protocol_runtime_start(
                app->protocol_runtime,
                package,
                app->current_api,
                tumospectrum_protocol_observation_callback,
                app);
        }
        tumospectrum_refresh_protocol(app);
        return true;
    }
    if(event->key == InputKeyRight && !listening) {
        if(tumospectrum_protocol_has_demo(tumospectrum_selected_protocol(app))) {
            tumospectrum_protocol_demo(app);
        } else {
            tumospectrum_protocol_delete(app);
        }
        return true;
    }
    return false;
}

static uint32_t tumospectrum_protocol_previous(void* context) {
    TumoSpectrumApp* app = context;
    tumospectrum_protocol_runtime_stop(app->protocol_runtime);
    return TumoSpectrumViewProfileMenu;
}

static void tumospectrum_protocol_profile_callback(void* context, uint32_t index) {
    TumoSpectrumApp* app = context;
    app->selected_protocol_package = index;
    app->has_protocol_observation = false;
    app->protocol_demo_result = false;
    const ProtocolProfilePackage* package = tumospectrum_selected_protocol(app);
    app->protocol_result_status = package != NULL ? package->status :
                                                    ProtocolProfileStatusInvalidArgument;
    tumospectrum_refresh_protocol(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewProtocol);
}

static void tumospectrum_open_protocol_profiles(TumoSpectrumApp* app) {
    tumospectrum_protocol_runtime_stop(app->protocol_runtime);
    app->protocol_package_count = protocol_profile_packages_load(
        app->storage, app->current_api, app->protocol_packages, ProtocolProfileMaximumProfiles);
    submenu_reset(app->profile_menu);
    submenu_set_header(app->profile_menu, "Protocol Profiles");
    if(app->protocol_package_count == 0U) {
        submenu_add_item(
            app->profile_menu,
            "No profiles installed",
            ProtocolProfileMaximumProfiles,
            tumospectrum_protocol_profile_callback,
            app);
    } else {
        for(size_t index = 0U; index < app->protocol_package_count; index++) {
            const ProtocolProfilePackage* package = &app->protocol_packages[index];
            char label[48];
            snprintf(
                label,
                sizeof(label),
                "%s%s",
                package->name[0] ? package->name : package->filename,
                package->status == ProtocolProfileStatusOk ? "" : " [blocked]");
            submenu_add_item(
                app->profile_menu,
                label,
                (uint32_t)index,
                tumospectrum_protocol_profile_callback,
                app);
        }
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewProfileMenu);
}

static void tumospectrum_tick(void* context) {
    TumoSpectrumApp* app = context;
    if(tumospectrum_band_map_is_running(app->band_map)) {
        tumospectrum_band_map_tick(app->band_map);
        tumospectrum_refresh_band_map(app);
    }
    if(tumospectrum_protocol_runtime_is_listening(app->protocol_runtime)) {
        tumospectrum_protocol_runtime_tick(app->protocol_runtime);
        tumospectrum_refresh_protocol(app);
    }
}

static uint32_t tumospectrum_band_map_previous(void* context) {
    TumoSpectrumApp* app = context;
    tumospectrum_band_map_stop(app->band_map);
    return TumoSpectrumViewMenu;
}

static uint32_t tumospectrum_result_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewMenu;
}

static uint32_t tumospectrum_actions_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewResult;
}

static uint32_t tumospectrum_set_actions_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewCaptureSet;
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

static uint32_t tumospectrum_profile_name_previous(void* context) {
    UNUSED(context);
    return TumoSpectrumViewSetActions;
}

static bool tumospectrum_back(void* context) {
    TumoSpectrumApp* app = context;
    tumospectrum_band_map_stop(app->band_map);
    tumospectrum_protocol_runtime_stop(app->protocol_runtime);
    view_dispatcher_stop(app->view_dispatcher);
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

static void tumospectrum_start_capture_route(
    TumoSpectrumApp* app,
    TumoSpectrumCaptureType type,
    TumoSpectrumCaptureResume resume,
    uint32_t frequency_hz) {
    const char* target = type == TumoSpectrumCaptureSubGhzRaw ? "Sub-GHz" : "Infrared";
    FuriString* self_path = furi_string_alloc();
    const bool self_found = loader_get_application_launch_path(app->loader, self_path);
    if(!self_found ||
       !tumospectrum_capture_flow_prepare_route(app->storage, type, resume, frequency_hz)) {
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

    char launch_argument[48];
    const char* argument = TUMOSPECTRUM_CAPTURE_LAUNCH_ARG;
    if(resume == TumoSpectrumCaptureResumeBandMap) {
        const int length = snprintf(
            launch_argument,
            sizeof(launch_argument),
            TUMOSPECTRUM_CAPTURE_LAUNCH_ARG ":%lu",
            (unsigned long)frequency_hz);
        if(length <= 0 || (size_t)length >= sizeof(launch_argument)) {
            furi_string_free(self_path);
            notification_message(app->notification, &tumospectrum_sequence_error);
            return;
        }
        argument = launch_argument;
    }

    tumospectrum_band_map_stop(app->band_map);
    tumospectrum_protocol_runtime_stop(app->protocol_runtime);
    loader_clear_launch_queue(app->loader);
    loader_enqueue_launch(app->loader, target, argument, LoaderDeferredLaunchFlagGui);
    loader_enqueue_launch(
        app->loader, furi_string_get_cstr(self_path), NULL, LoaderDeferredLaunchFlagGui);
    furi_string_free(self_path);
    view_dispatcher_stop(app->view_dispatcher);
}

static void tumospectrum_start_capture(TumoSpectrumApp* app, TumoSpectrumCaptureType type) {
    tumospectrum_start_capture_route(app, type, TumoSpectrumCaptureResumeResult, 0U);
}

static void tumospectrum_start_smart_capture(TumoSpectrumApp* app, uint32_t frequency_hz) {
    tumospectrum_start_capture_route(
        app, TumoSpectrumCaptureSubGhzRaw, TumoSpectrumCaptureResumeBandMap, frequency_hz);
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

static void tumospectrum_capture_set_clear_session(TumoSpectrumApp* app) {
    if(!app->smart_capture_set) return;
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Clear Smart Set?", 64, 4, AlignCenter, AlignTop);
    dialog_message_set_text(
        message,
        "Forget the current\nBand Map capture list?\nRAW files stay on SD.",
        64,
        28,
        AlignCenter,
        AlignCenter);
    dialog_message_set_buttons(message, "Clear", NULL, "Keep");
    const DialogMessageButton result = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
    if(result != DialogMessageButtonLeft) return;

    if(tumospectrum_band_session_clear(app->storage)) {
        app->smart_capture_set = false;
        app->smart_capture_frequency_hz = 0U;
        app->smart_capture_sample_count = 0U;
        memset(&app->capture_set, 0, sizeof(app->capture_set));
        notification_message(app->notification, &tumospectrum_sequence_ok);
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewMenu);
    } else {
        notification_message(app->notification, &tumospectrum_sequence_error);
        dialog_message_show_storage_error(app->dialogs, "Cannot clear\nsession");
    }
}

static void tumospectrum_capture_set_action_callback(void* context, uint32_t index) {
    TumoSpectrumApp* app = context;
    switch(index) {
    case TumoSpectrumSetActionCreateProfile:
        tumospectrum_capture_set_begin_profile(app);
        break;
    case TumoSpectrumSetActionCompanion:
        tumospectrum_capture_set_send(app);
        break;
    case TumoSpectrumSetActionHandoff:
        if(app->capture_set.inference.replay_class == TumoSpectrumReplayStaticLike &&
           app->capture_set.sample_count > 0U) {
            tumospectrum_handoff_capture(
                app, &app->capture_set.samples[0], TUMOSPECTRUM_LATEST_SET_ARG);
        }
        break;
    case TumoSpectrumSetActionDetails:
        tumospectrum_capture_set_show_details(app);
        break;
    case TumoSpectrumSetActionClearSession:
        tumospectrum_capture_set_clear_session(app);
        break;
    default:
        break;
    }
}

static void tumospectrum_capture_set_build_actions(TumoSpectrumApp* app) {
    submenu_reset(app->set_actions);
    submenu_set_header(app->set_actions, "Capture Set Actions");
    if(app->capture_set.type == TumoSpectrumCaptureSubGhzRaw && app->capture_set.inferred &&
       app->capture_set.inference.compatible &&
       app->capture_set.sample_count >= TUMOSPECTRUM_SET_MIN_SAMPLES) {
        submenu_add_item(
            app->set_actions,
            "Create Live Profile",
            TumoSpectrumSetActionCreateProfile,
            tumospectrum_capture_set_action_callback,
            app);
    }
    submenu_add_item(
        app->set_actions,
        "Send to Companion",
        TumoSpectrumSetActionCompanion,
        tumospectrum_capture_set_action_callback,
        app);
    if(app->capture_set.inference.replay_class == TumoSpectrumReplayStaticLike &&
       app->capture_set.sample_count > 0U) {
        submenu_add_item(
            app->set_actions,
            app->capture_set.type == TumoSpectrumCaptureSubGhzRaw ? "Open Source in Sub-GHz" :
                                                                    "Open Source in Infrared",
            TumoSpectrumSetActionHandoff,
            tumospectrum_capture_set_action_callback,
            app);
    }
    submenu_add_item(
        app->set_actions,
        "Profile Details",
        TumoSpectrumSetActionDetails,
        tumospectrum_capture_set_action_callback,
        app);
    if(app->smart_capture_set) {
        submenu_add_item(
            app->set_actions,
            "Clear Smart Session",
            TumoSpectrumSetActionClearSession,
            tumospectrum_capture_set_action_callback,
            app);
    }
}

static void tumospectrum_capture_set_profile_done(void* context) {
    TumoSpectrumApp* app = context;
    ProtocolProfilePackage built = {0};
    const TumoSpectrumProfileBuildStatus build_status = tumospectrum_profile_builder_build(
        &app->capture_set, app->profile_name_buffer, app->current_api, &built);
    if(build_status != TumoSpectrumProfileBuildOk) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Profile Rejected",
            tumospectrum_profile_builder_status_name(build_status),
            TumoSpectrumViewCaptureSet);
        return;
    }

    char saved_path[TUMOSPECTRUM_PATH_SIZE];
    if(!protocol_profile_package_save(app->storage, &built, saved_path, sizeof(saved_path))) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Save Failed",
            "The profile is valid, but its atomic SD save failed. Check the SD card and retry.",
            TumoSpectrumViewCaptureSet);
        return;
    }

    tumospectrum_open_protocol_profiles(app);
    size_t selected = app->protocol_package_count;
    for(size_t index = 0U; index < app->protocol_package_count; index++) {
        if(strcmp(app->protocol_packages[index].profile_id, built.profile_id) == 0) {
            selected = index;
            break;
        }
    }
    if(selected >= app->protocol_package_count) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Reload Failed",
            "The profile was saved, but could not be reopened. Check the Profiles folder.",
            TumoSpectrumViewCaptureSet);
        return;
    }

    app->selected_protocol_package = selected;
    if(app->smart_capture_set) {
        tumospectrum_band_session_clear(app->storage);
        app->smart_capture_set = false;
        app->smart_capture_frequency_hz = 0U;
        app->smart_capture_sample_count = 0U;
    }
    app->has_protocol_observation = false;
    app->protocol_demo_result = false;
    app->protocol_result_status = ProtocolProfileStatusNoMatch;
    tumospectrum_protocol_runtime_start(
        app->protocol_runtime,
        &app->protocol_packages[selected],
        app->current_api,
        tumospectrum_protocol_observation_callback,
        app);
    tumospectrum_refresh_protocol(app);
    notification_message(app->notification, &tumospectrum_sequence_ok);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewProtocol);
}

static void tumospectrum_capture_set_begin_profile(TumoSpectrumApp* app) {
    snprintf(
        app->profile_name_buffer,
        sizeof(app->profile_name_buffer),
        "%.14s %.14s",
        app->capture_set.device_label[0] ? app->capture_set.device_label : "Device",
        app->capture_set.control_label[0] ? app->capture_set.control_label : "Control");
    text_input_reset(app->profile_name_input);
    text_input_set_header_text(app->profile_name_input, "Live profile name");
    text_input_set_result_callback(
        app->profile_name_input,
        tumospectrum_capture_set_profile_done,
        app,
        app->profile_name_buffer,
        sizeof(app->profile_name_buffer),
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewProfileName);
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
    app->smart_capture_set = false;
    app->smart_capture_frequency_hz = 0U;
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
    app->smart_capture_set = false;
    app->smart_capture_frequency_hz = 0U;
    strlcpy(app->set_profile_path, TUMOSPECTRUM_LATEST_SET, sizeof(app->set_profile_path));
    strlcpy(app->set_report_path, TUMOSPECTRUM_LATEST_SET_REPORT, sizeof(app->set_report_path));
    tumospectrum_refresh_set(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewCaptureSet);
}

static bool tumospectrum_load_band_session_set(
    TumoSpectrumApp* app,
    const TumoSpectrumBandSession* session) {
    if(!app || !session || session->frequency_hz == 0U || session->sample_count == 0U) {
        return false;
    }

    memset(&app->capture_set, 0, sizeof(app->capture_set));
    app->capture_set.type = TumoSpectrumCaptureSubGhzRaw;
    strlcpy(app->capture_set.device_label, "Band Map", sizeof(app->capture_set.device_label));
    snprintf(
        app->capture_set.control_label,
        sizeof(app->capture_set.control_label),
        "%03lu.%03lu MHz",
        (unsigned long)(session->frequency_hz / 1000000U),
        (unsigned long)((session->frequency_hz % 1000000U) / 1000U));

    for(size_t index = 0U; index < session->sample_count &&
                           app->capture_set.sample_count < TUMOSPECTRUM_SET_MAX_SAMPLES;
        index++) {
        TumoSpectrumCapture* sample = &app->capture_set.samples[app->capture_set.sample_count];
        tumospectrum_parse_selected(
            app, TumoSpectrumCaptureSubGhzRaw, session->sample_paths[index], sample);
        if(sample->status == TumoSpectrumStatusOk &&
           sample->type == TumoSpectrumCaptureSubGhzRaw &&
           tumospectrum_type_has_timings(sample->type)) {
            app->capture_set.sample_count++;
        } else {
            memset(sample, 0, sizeof(*sample));
        }
    }

    if(app->capture_set.sample_count == 0U) return false;
    app->smart_capture_set = true;
    app->smart_capture_frequency_hz = session->frequency_hz;
    app->smart_capture_sample_count = (uint8_t)app->capture_set.sample_count;
    app->set_profile_path[0] = '\0';
    app->set_report_path[0] = '\0';
    return true;
}

static void tumospectrum_open_band_map(TumoSpectrumApp* app) {
    TumoSpectrumBandSession session;
    app->smart_capture_sample_count = tumospectrum_band_session_load(app->storage, &session) ?
                                          (uint8_t)session.sample_count :
                                          0U;
    tumospectrum_protocol_runtime_stop(app->protocol_runtime);
    if(!tumospectrum_band_map_start(app->band_map, true)) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Band Map",
            "Radio is busy or unavailable. Close other Sub-GHz tools and try again.",
            TumoSpectrumViewMenu);
        return;
    }
    tumospectrum_refresh_band_map(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewBandMap);
}

static void tumospectrum_resume_band_map_capture(
    TumoSpectrumApp* app,
    uint32_t frequency_hz,
    const char* path,
    bool needs_selection) {
    TumoSpectrumCapture* capture = &app->capture;
    memset(capture, 0, sizeof(*capture));
    if(path && path[0]) {
        tumospectrum_parse_selected(app, TumoSpectrumCaptureSubGhzRaw, path, capture);
    } else if(needs_selection) {
        if(!tumospectrum_select_file(app, TumoSpectrumCaptureSubGhzRaw, capture)) return;
    } else {
        tumospectrum_show_text(
            app,
            "Smart Capture",
            "No new RAW capture was found. Existing files were not changed.",
            TumoSpectrumViewMenu);
        return;
    }

    if(capture->status != TumoSpectrumStatusOk || capture->type != TumoSpectrumCaptureSubGhzRaw ||
       !tumospectrum_type_has_timings(capture->type)) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "RAW Required",
            "Smart Capture needs a saved Sub-GHz RAW file with timing data.",
            TumoSpectrumViewMenu);
        return;
    }

    TumoSpectrumBandSession session;
    if(!tumospectrum_band_session_append(app->storage, frequency_hz, capture->path, &session) ||
       !tumospectrum_load_band_session_set(app, &session)) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Smart Capture",
            "The RAW file is valid, but the bounded capture session could not be saved.",
            TumoSpectrumViewMenu);
        return;
    }

    tumospectrum_refresh_set(app);
    notification_message(app->notification, &tumospectrum_sequence_ok);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewCaptureSet);
}

static bool tumospectrum_resume_capture(TumoSpectrumApp* app) {
    TumoSpectrumCaptureType type = TumoSpectrumCaptureNone;
    TumoSpectrumCaptureResume resume = TumoSpectrumCaptureResumeResult;
    uint32_t frequency_hz = 0U;
    char path[TUMOSPECTRUM_PATH_SIZE];
    bool needs_selection = false;
    if(!tumospectrum_capture_flow_resume_route(
           app->storage, &type, &resume, &frequency_hz, path, sizeof(path), &needs_selection)) {
        return false;
    }
    if(resume == TumoSpectrumCaptureResumeBandMap) {
        tumospectrum_resume_band_map_capture(app, frequency_hz, path, needs_selection);
        return true;
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
    case TumoSpectrumMenuBandMap:
        tumospectrum_open_band_map(app);
        break;
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
    case TumoSpectrumMenuProtocolProfiles:
        tumospectrum_open_protocol_profiles(app);
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
            "TumoSpectrum 3.0",
            "Autonomous receive-only Band Map, Smart Capture, profile building and multi-capture signal research workspace.\n\n"
            "Band Map: Left/Right tunes, Up changes band, Down selects radio. Long Up zooms, long Down snaps to peak and long OK holds the scan.\n\n"
            "Protocol Profiles performs bounded receive-only decoding on Flipper and logs changed observations to SD.\n\n"
            "A Sub-GHz capture set can build a receive-only live profile from three or four RAW recordings without a computer.\n\n"
            "Source files are never modified. Static-like is not a cryptographic or replay guarantee. Any handoff stays in stock apps and their regional TX gate.\n\n"
            "github.com/squazaryu/tumoflip\nIssues #123 / #153 / #155 / #170",
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

static void tumospectrum_handoff_capture(
    TumoSpectrumApp* app,
    const TumoSpectrumCapture* capture,
    const char* return_argument) {
    const char* target = NULL;
    if(capture->type == TumoSpectrumCaptureSubGhzRaw ||
       capture->type == TumoSpectrumCaptureSubGhzDecoded) {
        target = "Sub-GHz";
    } else if(
        capture->type == TumoSpectrumCaptureInfraredRaw ||
        capture->type == TumoSpectrumCaptureInfraredParsed) {
        target = "Infrared";
    }
    if(!target || !storage_file_exists(app->storage, capture->path)) {
        notification_message(app->notification, &tumospectrum_sequence_error);
        tumospectrum_show_text(
            app,
            "Source Missing",
            "The saved source capture is no longer available on the SD card.",
            return_argument ? TumoSpectrumViewCaptureSet : TumoSpectrumViewResult);
        return;
    }

    loader_clear_launch_queue(app->loader);
    loader_enqueue_launch(app->loader, target, capture->path, LoaderDeferredLaunchFlagGui);
    FuriString* self_path = furi_string_alloc();
    if(loader_get_application_launch_path(app->loader, self_path)) {
        loader_enqueue_launch(
            app->loader,
            furi_string_get_cstr(self_path),
            return_argument,
            LoaderDeferredLaunchFlagGui);
    }
    furi_string_free(self_path);
    view_dispatcher_stop(app->view_dispatcher);
}

static void tumospectrum_handoff(TumoSpectrumApp* app) {
    tumospectrum_handoff_capture(app, &app->capture, NULL);
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
    app->protocol_runtime = tumospectrum_protocol_runtime_alloc();
    if(app->protocol_runtime == NULL) {
        free(app);
        return NULL;
    }
    app->band_map = tumospectrum_band_map_alloc();
    if(app->band_map == NULL) {
        tumospectrum_protocol_runtime_free(app->protocol_runtime);
        free(app);
        return NULL;
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->loader = furi_record_open(RECORD_LOADER);
    app->bt = furi_record_open(RECORD_BT);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->actions = submenu_alloc();
    app->set_actions = submenu_alloc();
    app->profile_menu = submenu_alloc();
    app->result_view = view_alloc();
    app->capture_set_view = view_alloc();
    app->protocol_view = view_alloc();
    app->band_map_view = view_alloc();
    app->text_box = text_box_alloc();
    app->note_input = text_input_alloc();
    app->set_input = text_input_alloc();
    app->profile_name_input = text_input_alloc();
    app->text = furi_string_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, tumospectrum_back);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, tumospectrum_tick, 100U);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    submenu_set_header(app->menu, "TumoSpectrum");
    submenu_add_item(
        app->menu, "Band Map", TumoSpectrumMenuBandMap, tumospectrum_menu_callback, app);
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
        app->menu,
        "Protocol Profiles",
        TumoSpectrumMenuProtocolProfiles,
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
    view_set_previous_callback(
        submenu_get_view(app->set_actions), tumospectrum_set_actions_previous);
    view_set_previous_callback(
        submenu_get_view(app->profile_menu), tumospectrum_text_previous_menu);
    view_allocate_model(
        app->protocol_view, ViewModelTypeLocking, sizeof(TumoSpectrumProtocolModel));
    view_set_context(app->protocol_view, app);
    view_set_draw_callback(app->protocol_view, tumospectrum_protocol_draw);
    view_set_input_callback(app->protocol_view, tumospectrum_protocol_input);
    view_set_previous_callback(app->protocol_view, tumospectrum_protocol_previous);
    view_allocate_model(
        app->band_map_view, ViewModelTypeLocking, sizeof(TumoSpectrumBandMapModel));
    view_set_context(app->band_map_view, app);
    view_set_draw_callback(app->band_map_view, tumospectrum_band_map_draw);
    view_set_input_callback(app->band_map_view, tumospectrum_band_map_input);
    view_set_previous_callback(app->band_map_view, tumospectrum_band_map_previous);

    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(text_box_get_view(app->text_box), tumospectrum_text_previous_menu);
    view_set_previous_callback(text_input_get_view(app->note_input), tumospectrum_note_previous);
    view_set_previous_callback(
        text_input_get_view(app->set_input), tumospectrum_set_input_previous);
    view_set_previous_callback(
        text_input_get_view(app->profile_name_input), tumospectrum_profile_name_previous);

    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->view_dispatcher, TumoSpectrumViewBandMap, app->band_map_view);
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
        app->view_dispatcher, TumoSpectrumViewSetActions, submenu_get_view(app->set_actions));
    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewSetInput, text_input_get_view(app->set_input));
    view_dispatcher_add_view(
        app->view_dispatcher,
        TumoSpectrumViewProfileName,
        text_input_get_view(app->profile_name_input));
    view_dispatcher_add_view(
        app->view_dispatcher, TumoSpectrumViewProfileMenu, submenu_get_view(app->profile_menu));
    view_dispatcher_add_view(app->view_dispatcher, TumoSpectrumViewProtocol, app->protocol_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoSpectrumViewMenu);
    tumospectrum_storage_prepare(app->storage);
    protocol_profile_storage_prepare(app->storage);
    uint16_t api_major = 0U;
    uint16_t api_minor = 0U;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    UNUSED(api_minor);
    app->current_api = api_major;
    app->protocol_result_status = ProtocolProfileStatusNoMatch;
    return app;
}

static void tumospectrum_free(TumoSpectrumApp* app) {
    tumospectrum_band_map_stop(app->band_map);
    tumospectrum_protocol_runtime_stop(app->protocol_runtime);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewProtocol);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewProfileMenu);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewProfileName);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewSetInput);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewSetActions);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewCaptureSet);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewNote);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewText);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewActions);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewResult);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewBandMap);
    view_dispatcher_remove_view(app->view_dispatcher, TumoSpectrumViewMenu);
    text_input_free(app->profile_name_input);
    text_input_free(app->set_input);
    text_input_free(app->note_input);
    text_box_free(app->text_box);
    view_free(app->protocol_view);
    view_free(app->band_map_view);
    view_free(app->capture_set_view);
    view_free(app->result_view);
    submenu_free(app->set_actions);
    submenu_free(app->actions);
    submenu_free(app->profile_menu);
    submenu_free(app->menu);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->text);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    tumospectrum_band_map_free(app->band_map);
    tumospectrum_protocol_runtime_free(app->protocol_runtime);
    free(app);
}

int32_t signal_workbench_app(void* context) {
    TumoSpectrumApp* app = tumospectrum_alloc();
    if(!app) return -1;
    if(context && strcmp(context, TUMOSPECTRUM_LATEST_SET_ARG) == 0) {
        tumospectrum_open_latest_set(app);
    } else if(context && strcmp(context, TUMOSPECTRUM_PROFILES_ARG) == 0) {
        tumospectrum_open_protocol_profiles(app);
    } else {
        tumospectrum_resume_capture(app);
    }
    view_dispatcher_run(app->view_dispatcher);
    tumospectrum_free(app);
    return 0;
}
