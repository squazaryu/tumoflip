#include "protocol_profile_storage.h"
#include "protocol_compiler_icons.h"

#include <dialogs/dialogs.h>
#include <furi.h>
#include <furi_hal_info.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTOCOL_COMPILER_QUEUE_DEPTH 8U
#define PROTOCOL_COMPILER_SUBGHZ_DIR  EXT_PATH("subghz")

typedef enum {
    ProtocolCompilerScreenOverview,
    ProtocolCompilerScreenActions,
    ProtocolCompilerScreenResult,
    ProtocolCompilerScreenAbout,
} ProtocolCompilerScreen;

typedef enum {
    ProtocolCompilerActionDemo,
    ProtocolCompilerActionOpen,
    ProtocolCompilerActionCount,
} ProtocolCompilerAction;

typedef struct {
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    ProtocolProfilePackage packages[ProtocolProfileMaximumProfiles];
    size_t package_count;
    size_t selected_profile;
    ProtocolCompilerAction selected_action;
    ProtocolCompilerScreen screen;
    ProtocolProfileStatus result_status;
    ProtocolProfileCaptureInfo capture_info;
    ProtocolProfileDecodeResult decode_result;
    char result_detail[32];
    char result_source[24];
    char last_path[160];
    bool running;
} ProtocolCompilerApp;

static const ProtocolProfilePackage* protocol_compiler_selected(const ProtocolCompilerApp* app) {
    if(app->package_count == 0U || app->selected_profile >= app->package_count) return NULL;
    return &app->packages[app->selected_profile];
}

static void protocol_compiler_draw_status(Canvas* canvas, const char* title, const char* status) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, title);
    canvas_draw_rframe(canvas, 2, 13, 124, 13, 3);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, status);
}

static void
    protocol_compiler_draw_menu_row(Canvas* canvas, uint8_t y, const char* label, bool selected) {
    if(selected) {
        canvas_draw_box(canvas, 8, y - 8, 112, 10);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 13, y, label);
    if(selected) canvas_set_color(canvas, ColorBlack);
}

static void protocol_compiler_draw_overview(Canvas* canvas, const ProtocolCompilerApp* app) {
    const ProtocolProfilePackage* package = protocol_compiler_selected(app);
    char status[32];
    char line[48];
    if(package == NULL) {
        strlcpy(status, "No .tproto profiles", sizeof(status));
    } else {
        snprintf(
            status,
            sizeof(status),
            "%u/%u  %s",
            (unsigned)(app->selected_profile + 1U),
            (unsigned)app->package_count,
            protocol_profile_status_name(package->status));
    }
    protocol_compiler_draw_status(canvas, "Protocol Compiler", status);
    canvas_set_font(canvas, FontSecondary);
    if(package == NULL) {
        canvas_draw_str(canvas, 4, 38, "Install Tumoflip FW Packages");
        canvas_draw_str(canvas, 4, 48, "profiles/*.tproto");
        elements_button_center(canvas, "Empty");
        return;
    }

    snprintf(line, sizeof(line), "%.23s", package->name[0] ? package->name : package->filename);
    canvas_draw_str(canvas, 4, 37, line);
    if(package->status == ProtocolProfileStatusOk) {
        if(package->profile.frequency_hz > 0U) {
            snprintf(
                line,
                sizeof(line),
                "%u bit  %u%%  %lu.%03lu MHz",
                package->profile.bit_count,
                package->profile.confidence,
                (unsigned long)(package->profile.frequency_hz / 1000000U),
                (unsigned long)((package->profile.frequency_hz / 1000U) % 1000U));
        } else {
            snprintf(
                line,
                sizeof(line),
                "%u bit  %u%%  freq unknown",
                package->profile.bit_count,
                package->profile.confidence);
        }
    } else {
        strlcpy(line, "Profile blocked", sizeof(line));
    }
    canvas_draw_str(canvas, 4, 48, line);
    elements_button_left(canvas, "Prev");
    elements_button_center(canvas, package->status == ProtocolProfileStatusOk ? "Open" : "Info");
    elements_button_right(canvas, "Next");
}

static void protocol_compiler_draw_actions(Canvas* canvas, const ProtocolCompilerApp* app) {
    const ProtocolProfilePackage* package = protocol_compiler_selected(app);
    char status[24];
    snprintf(status, sizeof(status), "%.22s", package != NULL ? package->name : "No profile");
    protocol_compiler_draw_status(canvas, "Protocol Compiler", status);
    canvas_set_font(canvas, FontSecondary);
    protocol_compiler_draw_menu_row(
        canvas, 36, "Validate packaged demo", app->selected_action == ProtocolCompilerActionDemo);
    protocol_compiler_draw_menu_row(
        canvas, 47, "Open RAW capture", app->selected_action == ProtocolCompilerActionOpen);
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Select");
    elements_button_right(canvas, "Info");
}

static void protocol_compiler_draw_result(Canvas* canvas, const ProtocolCompilerApp* app) {
    char line[48];
    char status[32];
    if(app->result_status == ProtocolProfileStatusOk) {
        snprintf(status, sizeof(status), "OK / %.18s", app->result_source);
    } else {
        strlcpy(status, app->result_detail, sizeof(status));
    }
    protocol_compiler_draw_status(canvas, "Validation", status);
    canvas_set_font(canvas, FontSecondary);
    if(app->result_status == ProtocolProfileStatusOk) {
        snprintf(
            line,
            sizeof(line),
            "Value 0x%0*llX / %u bit",
            (int)((app->decode_result.bit_count + 3U) / 4U),
            (unsigned long long)app->decode_result.value,
            app->decode_result.bit_count);
        canvas_draw_str(canvas, 4, 38, line);
        snprintf(
            line,
            sizeof(line),
            "Offset %u / pulses %u%s",
            app->decode_result.start_offset,
            (unsigned)app->capture_info.pulse_count,
            app->capture_info.truncated ? "+" : "");
        canvas_draw_str(canvas, 4, 48, line);
    } else {
        snprintf(line, sizeof(line), "File %.20s", app->result_source);
        canvas_draw_str(canvas, 4, 38, line);
        snprintf(
            line,
            sizeof(line),
            "Capture pulses: %u%s",
            (unsigned)app->capture_info.pulse_count,
            app->capture_info.truncated ? " (bounded)" : "");
        canvas_draw_str(canvas, 4, 48, line);
    }
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Again");
    elements_button_right(canvas, "Info");
}

static void protocol_compiler_draw_about(Canvas* canvas, const ProtocolCompilerApp* app) {
    const ProtocolProfilePackage* package = protocol_compiler_selected(app);
    char line[48];
    protocol_compiler_draw_status(canvas, "Profile Info", "RX-only v0.1 github:#68");
    canvas_set_font(canvas, FontSecondary);
    if(package != NULL && package->status == ProtocolProfileStatusOk) {
        snprintf(
            line,
            sizeof(line),
            "Train %uc %uf / tol %u%%",
            package->training_captures,
            package->training_frames,
            package->profile.tolerance_percent);
        canvas_draw_str(canvas, 4, 36, line);
        snprintf(
            line,
            sizeof(line),
            "%.11s / %.8s",
            protocol_profile_checksum_name(package->profile.checksum),
            package->ambiguity);
        canvas_draw_str(canvas, 4, 46, line);
    } else if(package != NULL) {
        snprintf(line, sizeof(line), "%.23s", package->filename);
        canvas_draw_str(canvas, 4, 36, line);
        canvas_draw_str(canvas, 4, 46, protocol_profile_status_name(package->status));
    } else {
        canvas_draw_str(canvas, 4, 36, "No installed profile");
    }
    elements_button_left(canvas, "Close");
}

static void protocol_compiler_draw_callback(Canvas* canvas, void* context) {
    ProtocolCompilerApp* app = context;
    canvas_clear(canvas);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->screen == ProtocolCompilerScreenOverview) {
        protocol_compiler_draw_overview(canvas, app);
    } else if(app->screen == ProtocolCompilerScreenActions) {
        protocol_compiler_draw_actions(canvas, app);
    } else if(app->screen == ProtocolCompilerScreenResult) {
        protocol_compiler_draw_result(canvas, app);
    } else {
        protocol_compiler_draw_about(canvas, app);
    }
    furi_mutex_release(app->mutex);
}

static void protocol_compiler_input_callback(InputEvent* event, void* context) {
    ProtocolCompilerApp* app = context;
    furi_message_queue_put(app->queue, event, 0U);
}

static void protocol_compiler_set_result(
    ProtocolCompilerApp* app,
    ProtocolProfileStatus status,
    const char* detail,
    const char* source,
    const ProtocolProfileCaptureInfo* capture_info,
    const ProtocolProfileDecodeResult* decode_result) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->result_status = status;
    strlcpy(app->result_detail, detail, sizeof(app->result_detail));
    strlcpy(app->result_source, source, sizeof(app->result_source));
    if(capture_info != NULL) app->capture_info = *capture_info;
    if(decode_result != NULL) app->decode_result = *decode_result;
    app->screen = ProtocolCompilerScreenResult;
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

static const char* protocol_compiler_basename(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static void protocol_compiler_validate_path(ProtocolCompilerApp* app, const char* path) {
    const ProtocolProfilePackage* package = protocol_compiler_selected(app);
    ProtocolProfileCaptureInfo capture_info = {0};
    ProtocolProfileDecodeResult decode_result = {0};
    const char* source = protocol_compiler_basename(path);
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    strlcpy(app->last_path, path, sizeof(app->last_path));
    furi_mutex_release(app->mutex);
    if(package == NULL) {
        protocol_compiler_set_result(
            app, ProtocolProfileStatusInvalidArgument, "No profile", source, NULL, NULL);
        return;
    }
    if(package->status != ProtocolProfileStatusOk) {
        protocol_compiler_set_result(
            app,
            package->status,
            protocol_profile_status_name(package->status),
            source,
            NULL,
            NULL);
        return;
    }

    int32_t* pulses = malloc(sizeof(int32_t) * ProtocolProfileMaximumCapturePulses);
    if(pulses == NULL) {
        protocol_compiler_set_result(
            app, ProtocolProfileStatusInvalidArgument, "Not enough memory", source, NULL, NULL);
        return;
    }
    ProtocolProfileStatus status = protocol_profile_capture_load(
        app->storage, path, pulses, ProtocolProfileMaximumCapturePulses, &capture_info);
    const char* detail = protocol_profile_status_name(status);
    if(status == ProtocolProfileStatusOk && package->profile.frequency_hz > 0U &&
       capture_info.frequency_hz > 0U &&
       package->profile.frequency_hz != capture_info.frequency_hz) {
        status = ProtocolProfileStatusNoMatch;
        detail = "Frequency mismatch";
    } else if(status == ProtocolProfileStatusOk) {
        status = protocol_profile_decode(
            &package->profile,
            package->profile.minimum_api,
            pulses,
            capture_info.pulse_count,
            &decode_result);
        detail = protocol_profile_status_name(status);
    }
    free(pulses);
    protocol_compiler_set_result(app, status, detail, source, &capture_info, &decode_result);
}

static void protocol_compiler_open_file(ProtocolCompilerApp* app) {
    FuriString* path = furi_string_alloc_set(PROTOCOL_COMPILER_SUBGHZ_DIR);
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".sub", &I_sub1_10px);
    browser_options.base_path = PROTOCOL_COMPILER_SUBGHZ_DIR;
    const bool selected = dialog_file_browser_show(app->dialogs, path, path, &browser_options);
    if(selected) protocol_compiler_validate_path(app, furi_string_get_cstr(path));
    furi_string_free(path);
}

static void protocol_compiler_change_profile(ProtocolCompilerApp* app, int8_t delta) {
    if(app->package_count == 0U) return;
    if(delta < 0) {
        app->selected_profile = app->selected_profile == 0U ? app->package_count - 1U :
                                                              app->selected_profile - 1U;
    } else {
        app->selected_profile = (app->selected_profile + 1U) % app->package_count;
    }
}

static void protocol_compiler_handle_overview(ProtocolCompilerApp* app, const InputEvent* event) {
    if(event->key == InputKeyBack) {
        app->running = false;
    } else if(event->key == InputKeyLeft) {
        protocol_compiler_change_profile(app, -1);
    } else if(event->key == InputKeyRight) {
        protocol_compiler_change_profile(app, 1);
    } else if(event->key == InputKeyOk && app->package_count > 0U) {
        const ProtocolProfilePackage* package = protocol_compiler_selected(app);
        app->screen = package != NULL && package->status == ProtocolProfileStatusOk ?
                          ProtocolCompilerScreenActions :
                          ProtocolCompilerScreenAbout;
    }
}

static void protocol_compiler_handle_actions(ProtocolCompilerApp* app, const InputEvent* event) {
    if(event->key == InputKeyBack || event->key == InputKeyLeft) {
        app->screen = ProtocolCompilerScreenOverview;
    } else if(event->key == InputKeyRight) {
        app->screen = ProtocolCompilerScreenAbout;
    } else if(event->key == InputKeyUp) {
        app->selected_action = app->selected_action == 0 ? ProtocolCompilerActionCount - 1 :
                                                           app->selected_action - 1;
    } else if(event->key == InputKeyDown) {
        app->selected_action =
            (ProtocolCompilerAction)((app->selected_action + 1) % ProtocolCompilerActionCount);
    }
}

static void protocol_compiler_handle_result(ProtocolCompilerApp* app, const InputEvent* event) {
    if(event->key == InputKeyBack || event->key == InputKeyLeft) {
        app->screen = ProtocolCompilerScreenActions;
    } else if(event->key == InputKeyRight) {
        app->screen = ProtocolCompilerScreenAbout;
    }
}

static void protocol_compiler_handle_about(ProtocolCompilerApp* app, const InputEvent* event) {
    if(event->key == InputKeyBack || event->key == InputKeyLeft || event->key == InputKeyOk) {
        app->screen = app->package_count > 0U ? ProtocolCompilerScreenActions :
                                                ProtocolCompilerScreenOverview;
    }
}

static void protocol_compiler_handle_input(ProtocolCompilerApp* app, const InputEvent* event) {
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return;
    if(event->type == InputTypeRepeat && event->key != InputKeyUp && event->key != InputKeyDown &&
       event->key != InputKeyLeft && event->key != InputKeyRight) {
        return;
    }
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    const ProtocolCompilerScreen screen = app->screen;
    if(screen == ProtocolCompilerScreenOverview) {
        protocol_compiler_handle_overview(app, event);
    } else if(screen == ProtocolCompilerScreenActions) {
        if(event->key == InputKeyOk) {
            furi_mutex_release(app->mutex);
            if(app->selected_action == ProtocolCompilerActionDemo) {
                protocol_compiler_validate_path(app, PROTOCOL_PROFILE_DEMO_CAPTURE);
            } else {
                protocol_compiler_open_file(app);
            }
            return;
        }
        protocol_compiler_handle_actions(app, event);
    } else if(screen == ProtocolCompilerScreenResult) {
        if(event->key == InputKeyOk) {
            char last_path[sizeof(app->last_path)];
            strlcpy(last_path, app->last_path, sizeof(last_path));
            furi_mutex_release(app->mutex);
            protocol_compiler_validate_path(
                app, last_path[0] != '\0' ? last_path : PROTOCOL_PROFILE_DEMO_CAPTURE);
            return;
        }
        protocol_compiler_handle_result(app, event);
    } else {
        protocol_compiler_handle_about(app, event);
    }
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

static ProtocolCompilerApp* protocol_compiler_alloc(void) {
    ProtocolCompilerApp* app = malloc(sizeof(*app));
    if(app == NULL) return NULL;
    memset(app, 0, sizeof(*app));
    app->running = true;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(PROTOCOL_COMPILER_QUEUE_DEPTH, sizeof(InputEvent));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->gui = furi_record_open(RECORD_GUI);

    uint16_t api_major = 0U;
    uint16_t api_minor = 0U;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    UNUSED(api_minor);
    app->package_count = protocol_profile_packages_load(
        app->storage, api_major, app->packages, ProtocolProfileMaximumProfiles);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, protocol_compiler_draw_callback, app);
    view_port_input_callback_set(app->view_port, protocol_compiler_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void protocol_compiler_free(ProtocolCompilerApp* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
}

int32_t protocol_compiler_app(void* context) {
    UNUSED(context);
    ProtocolCompilerApp* app = protocol_compiler_alloc();
    if(app == NULL) return 255;

    view_port_update(app->view_port);
    while(app->running) {
        InputEvent event;
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) == FuriStatusOk) {
            protocol_compiler_handle_input(app, &event);
        }
    }
    protocol_compiler_free(app);
    return 0;
}
