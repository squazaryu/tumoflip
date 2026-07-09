/*!
 *  @file flipper-xremote/xremote_ac.c
 *  @license This project is released under the GNU GPLv3 License
 *
 * @brief Smart A/C controller integrated into Tumo XRemote.
 */

#include "xremote_ac.h"
#include "xremote_ac_engine.h"
#include "xremote_signal.h"

#include <ctype.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <toolbox/path.h>

#define XREMOTE_AC_STATUS_LEN      40
#define XREMOTE_AC_TITLE_LEN       24
#define XREMOTE_AC_TEXT_LEN        32
#define XREMOTE_AC_PATH_LEN        160
#define XREMOTE_AC_SWEEP_TEMP_MIN  16
#define XREMOTE_AC_SWEEP_TEMP_MAX  31

typedef enum {
    XRemoteAcMenuCreate,
    XRemoteAcMenuOpenTumo,
    XRemoteAcMenuOpenFzAc,
} XRemoteAcMenu;

typedef enum {
    XRemoteAcCreateIdle,
    XRemoteAcCreateName,
    XRemoteAcCreateCaptureOff,
    XRemoteAcCreatePresetName,
    XRemoteAcCreateCaptureTemp,
} XRemoteAcCreateStep;

typedef struct {
    XRemoteAppContext* app_ctx;
    Storage* storage;
    FuriString* file_path;
    TextInput* text_input;
    XRemoteSignalReceiver* ir_receiver;
    InfraredRemote* output_remote;
    InfraredSignal* capture_signal;
    XRemoteAcIndex index;
    char title[XREMOTE_AC_TITLE_LEN];
    char status[XREMOTE_AC_STATUS_LEN];
    char text_store[XREMOTE_AC_TEXT_LEN + 1];
    char create_name[XREMOTE_AC_TEXT_LEN + 1];
    char preset_name[XREMOTE_AC_PRESET_NAME_LEN];
    char output_path[XREMOTE_AC_PATH_LEN];
    XRemoteAcCreateStep create_step;
    uint8_t preset_index;
    uint8_t temp;
    uint8_t sweep_temp;
    uint8_t sweep_count;
    bool capture_ready;
    bool creator_active;
    bool ok_pressed;
    bool up_pressed;
    bool down_pressed;
    bool left_pressed;
    bool right_pressed;
    bool hold;
} XRemoteAcContext;

static uint32_t xremote_ac_submenu_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewSubmenu;
}

static uint32_t xremote_ac_remote_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewAcSmart;
}

static void xremote_ac_set_status(XRemoteAcContext* ctx, const char* status) {
    snprintf(ctx->status, sizeof(ctx->status), "%s", status);
}

static bool xremote_ac_sanitize_token(
    const char* input,
    char* output,
    size_t output_size,
    bool allow_space) {
    size_t pos = 0;
    bool last_was_sep = false;

    if(!input || output_size == 0) return false;

    for(size_t i = 0; input[i] != '\0' && pos + 1 < output_size; i++) {
        const unsigned char c = (unsigned char)input[i];
        if(isalnum(c) || c == '-' || c == '_') {
            output[pos++] = (char)c;
            last_was_sep = false;
        } else if(allow_space && c == ' ' && pos > 0 && !last_was_sep) {
            output[pos++] = ' ';
            last_was_sep = true;
        } else if(!allow_space && c == ' ' && pos > 0 && !last_was_sep) {
            output[pos++] = '_';
            last_was_sep = true;
        }
    }

    while(pos > 0 && (output[pos - 1] == ' ' || output[pos - 1] == '_')) pos--;
    output[pos] = '\0';
    return pos > 0;
}

static bool xremote_ac_make_unique_path(XRemoteAcContext* ctx, const char* safe_name) {
    int written = snprintf(
        ctx->output_path,
        sizeof(ctx->output_path),
        "%s/%s%s",
        XREMOTE_AC_DIR,
        safe_name,
        XREMOTE_APP_EXTENSION);
    if(written <= 0 || (size_t)written >= sizeof(ctx->output_path)) return false;
    if(!storage_file_exists(ctx->storage, ctx->output_path)) return true;

    char base[XREMOTE_AC_TEXT_LEN + 1];
    snprintf(base, sizeof(base), "%s", safe_name);

    for(uint8_t i = 2; i < 100; i++) {
        written = snprintf(
            ctx->output_path,
            sizeof(ctx->output_path),
            "%s/%s_%u%s",
            XREMOTE_AC_DIR,
            base,
            i,
            XREMOTE_APP_EXTENSION);
        if(written <= 0 || (size_t)written >= sizeof(ctx->output_path)) return false;
        if(!storage_file_exists(ctx->storage, ctx->output_path)) return true;
    }

    return false;
}

static bool xremote_ac_select_file(XRemoteAcContext* ctx, const char* base_path) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    storage_simply_mkdir(ctx->storage, base_path);

    if(ctx->file_path == NULL) {
        ctx->file_path = furi_string_alloc_set_str(base_path);
    } else {
        furi_string_set(ctx->file_path, base_path);
    }

    DialogsFileBrowserOptions browser;
    dialog_file_browser_set_basic_options(&browser, XREMOTE_APP_EXTENSION, &I_IR_Icon_10x10);
    browser.base_path = base_path;

    const bool selected = dialog_file_browser_show(dialogs, ctx->file_path, ctx->file_path, &browser);
    furi_record_close(RECORD_DIALOGS);
    return selected;
}

static bool xremote_ac_load_profile(XRemoteAcContext* ctx) {
    const char* path = furi_string_get_cstr(ctx->file_path);
    const bool loaded = xremote_ac_scan_file(ctx->storage, path, &ctx->index);

    FuriString* title = furi_string_alloc();
    path_extract_filename(ctx->file_path, title, true);
    snprintf(ctx->title, sizeof(ctx->title), "%.23s", furi_string_get_cstr(title));
    furi_string_free(title);

    ctx->preset_index = 0;
    ctx->temp = 22;

    if(loaded && ctx->index.preset_count > 0) {
        int32_t temp = xremote_ac_temp_bits_lowest(ctx->index.presets[0].temp_bits);
        if(temp > 0) ctx->temp = temp;
        xremote_ac_set_status(ctx, "Ready");
        return true;
    }

    xremote_ac_set_status(ctx, "No Smart AC frames");
    return false;
}

static bool xremote_ac_send_current(XRemoteAcContext* ctx) {
    if(ctx->index.preset_count == 0) {
        xremote_ac_set_status(ctx, "No preset");
        return false;
    }

    char signal_name[XREMOTE_AC_PRESET_NAME_LEN + 8];
    const XRemoteAcPresetInfo* preset = &ctx->index.presets[ctx->preset_index];
    xremote_ac_signal_name(signal_name, sizeof(signal_name), preset->name, ctx->temp);

    const bool sent = xremote_ac_send_named_signal(
        ctx->app_ctx, ctx->storage, furi_string_get_cstr(ctx->file_path), signal_name);
    xremote_ac_set_status(ctx, sent ? signal_name : "Missing frame");
    return sent;
}

static bool xremote_ac_send_off(XRemoteAcContext* ctx) {
    if(!ctx->index.has_off) {
        xremote_ac_set_status(ctx, "No Off frame");
        return false;
    }

    const bool sent = xremote_ac_send_named_signal(
        ctx->app_ctx, ctx->storage, furi_string_get_cstr(ctx->file_path), XREMOTE_AC_OFF_NAME);
    xremote_ac_set_status(ctx, sent ? "Off sent" : "Missing Off");
    return sent;
}

static void xremote_ac_select_preset(XRemoteAcContext* ctx, bool next) {
    if(ctx->index.preset_count == 0) {
        xremote_ac_set_status(ctx, "No preset");
        return;
    }

    if(next) {
        ctx->preset_index = (ctx->preset_index + 1) % ctx->index.preset_count;
    } else if(ctx->preset_index == 0) {
        ctx->preset_index = ctx->index.preset_count - 1;
    } else {
        ctx->preset_index--;
    }

    const uint32_t bits = ctx->index.presets[ctx->preset_index].temp_bits;
    const int32_t nearest = xremote_ac_temp_bits_nearest(bits, ctx->temp);
    if(nearest > 0) ctx->temp = nearest;
    xremote_ac_set_status(ctx, ctx->index.presets[ctx->preset_index].name);
}

static void xremote_ac_select_temp(XRemoteAcContext* ctx, bool up) {
    if(ctx->index.preset_count == 0) {
        xremote_ac_set_status(ctx, "No preset");
        return;
    }

    const uint32_t bits = ctx->index.presets[ctx->preset_index].temp_bits;
    const int32_t next = xremote_ac_temp_bits_next(bits, ctx->temp, up);
    if(next > 0) {
        ctx->temp = next;
        char status[XREMOTE_AC_STATUS_LEN];
        snprintf(status, sizeof(status), "Temp %u", ctx->temp);
        xremote_ac_set_status(ctx, status);
    } else {
        xremote_ac_set_status(ctx, "No temp frame");
    }
}

static void xremote_ac_stop_capture(XRemoteAcContext* ctx) {
    if(ctx->ir_receiver) xremote_signal_receiver_stop(ctx->ir_receiver);
}

static void xremote_ac_cancel_create(XRemoteAcContext* ctx) {
    xremote_app_assert_void(ctx);
    xremote_ac_stop_capture(ctx);
    ctx->creator_active = false;
    ctx->create_step = XRemoteAcCreateIdle;
    ctx->capture_ready = false;
    xremote_ac_set_status(ctx, "Create canceled");
}

static void xremote_ac_start_capture(XRemoteAcContext* ctx, XRemoteAcCreateStep step) {
    ctx->create_step = step;
    ctx->creator_active = true;
    ctx->capture_ready = false;
    xremote_ac_stop_capture(ctx);

    if(step == XRemoteAcCreateCaptureOff) {
        xremote_ac_set_status(ctx, "Waiting Off");
    } else {
        char status[XREMOTE_AC_STATUS_LEN];
        snprintf(status, sizeof(status), "Waiting %u C", ctx->sweep_temp);
        xremote_ac_set_status(ctx, status);
    }

    xremote_signal_receiver_start(ctx->ir_receiver);
    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartRemote);
}

static void xremote_ac_start_preset_name_input(XRemoteAcContext* ctx);

static bool xremote_ac_store_profile(XRemoteAcContext* ctx) {
    if(ctx->sweep_count == 0) {
        xremote_ac_set_status(ctx, "No temp frames");
        return false;
    }

    infrared_remote_set_name(ctx->output_remote, ctx->create_name);
    infrared_remote_set_path(ctx->output_remote, ctx->output_path);
    if(!infrared_remote_store(ctx->output_remote)) {
        xremote_ac_set_status(ctx, "Save failed");
        return false;
    }

    if(ctx->file_path == NULL) {
        ctx->file_path = furi_string_alloc_set_str(ctx->output_path);
    } else {
        furi_string_set(ctx->file_path, ctx->output_path);
    }

    ctx->creator_active = false;
    ctx->create_step = XRemoteAcCreateIdle;
    xremote_ac_load_profile(ctx);
    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartRemote);
    return true;
}

static void xremote_ac_save_capture(XRemoteAcContext* ctx) {
    if(!ctx->capture_ready) {
        xremote_ac_set_status(ctx, "Point AC remote");
        return;
    }

    if(ctx->create_step == XRemoteAcCreateCaptureOff) {
        infrared_remote_delete_button_by_name(ctx->output_remote, XREMOTE_AC_OFF_NAME);
        infrared_remote_push_button(ctx->output_remote, XREMOTE_AC_OFF_NAME, ctx->capture_signal);
        xremote_ac_stop_capture(ctx);
        xremote_ac_start_preset_name_input(ctx);
        return;
    }

    if(ctx->create_step == XRemoteAcCreateCaptureTemp) {
        char signal_name[XREMOTE_AC_PRESET_NAME_LEN + 8];
        xremote_ac_signal_name(signal_name, sizeof(signal_name), ctx->preset_name, ctx->sweep_temp);
        infrared_remote_delete_button_by_name(ctx->output_remote, signal_name);
        infrared_remote_push_button(ctx->output_remote, signal_name, ctx->capture_signal);
        ctx->sweep_count++;

        if(ctx->sweep_temp >= XREMOTE_AC_SWEEP_TEMP_MAX) {
            xremote_ac_stop_capture(ctx);
            xremote_ac_store_profile(ctx);
            return;
        }

        ctx->sweep_temp++;
        xremote_ac_start_capture(ctx, XRemoteAcCreateCaptureTemp);
    }
}

static void xremote_ac_rx_callback(void* context, InfraredSignal* signal) {
    XRemoteAcContext* ctx = context;
    xremote_app_assert_void(ctx);
    if(!ctx->creator_active || !ctx->ir_receiver) return;

    infrared_signal_set_signal(ctx->capture_signal, signal);
    ctx->capture_ready = true;
    xremote_ac_stop_capture(ctx);
    xremote_ac_set_status(ctx, "Captured. OK save");
    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartRemote);
}

static void xremote_ac_name_input_callback(void* context) {
    XRemoteAcContext* ctx = context;
    xremote_app_assert_void(ctx);

    char safe_name[XREMOTE_AC_TEXT_LEN + 1];
    if(!xremote_ac_sanitize_token(ctx->text_store, safe_name, sizeof(safe_name), false)) {
        xremote_ac_cancel_create(ctx);
        xremote_ac_set_status(ctx, "Invalid name");
        view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewAcSmart);
        return;
    }

    if(!xremote_ac_make_unique_path(ctx, safe_name)) {
        xremote_ac_cancel_create(ctx);
        xremote_ac_set_status(ctx, "Path too long");
        view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewAcSmart);
        return;
    }

    snprintf(ctx->create_name, sizeof(ctx->create_name), "%s", safe_name);
    snprintf(ctx->title, sizeof(ctx->title), "%.23s", ctx->create_name);
    infrared_remote_reset(ctx->output_remote);
    ctx->sweep_count = 0;
    ctx->sweep_temp = XREMOTE_AC_SWEEP_TEMP_MIN;
    xremote_ac_start_capture(ctx, XRemoteAcCreateCaptureOff);
}

static void xremote_ac_preset_input_callback(void* context) {
    XRemoteAcContext* ctx = context;
    xremote_app_assert_void(ctx);

    char safe_preset[XREMOTE_AC_PRESET_NAME_LEN];
    if(!xremote_ac_sanitize_token(ctx->text_store, safe_preset, sizeof(safe_preset), false)) {
        snprintf(safe_preset, sizeof(safe_preset), "Cool");
    }

    snprintf(ctx->preset_name, sizeof(ctx->preset_name), "%s", safe_preset);
    ctx->sweep_temp = XREMOTE_AC_SWEEP_TEMP_MIN;
    ctx->sweep_count = 0;
    xremote_ac_start_capture(ctx, XRemoteAcCreateCaptureTemp);
}

static uint32_t xremote_ac_text_input_exit_callback(void* context) {
    TextInput* text_input = context;
    XRemoteAcContext* ctx = text_input_get_validator_callback_context(text_input);
    xremote_ac_cancel_create(ctx);
    return XRemoteViewAcSmart;
}

static void xremote_ac_start_name_input(XRemoteAcContext* ctx) {
    ctx->creator_active = true;
    ctx->create_step = XRemoteAcCreateName;
    snprintf(ctx->text_store, sizeof(ctx->text_store), "AC_");

    text_input_reset(ctx->text_input);
    text_input_set_header_text(ctx->text_input, "New Smart AC");
    text_input_set_result_callback(
        ctx->text_input,
        xremote_ac_name_input_callback,
        ctx,
        ctx->text_store,
        XREMOTE_AC_TEXT_LEN,
        true);
    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewTextInput);
}

static void xremote_ac_start_preset_name_input(XRemoteAcContext* ctx) {
    ctx->creator_active = true;
    ctx->create_step = XRemoteAcCreatePresetName;
    snprintf(ctx->text_store, sizeof(ctx->text_store), "Cool");

    text_input_reset(ctx->text_input);
    text_input_set_header_text(ctx->text_input, "Preset name");
    text_input_set_result_callback(
        ctx->text_input,
        xremote_ac_preset_input_callback,
        ctx,
        ctx->text_store,
        XREMOTE_AC_PRESET_NAME_LEN - 1,
        true);
    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewTextInput);
}

static void xremote_ac_draw_horizontal(Canvas* canvas, XRemoteAcContext* ctx) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 9, "AC Smart");
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, ctx->title);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    const char* preset = ctx->index.preset_count ?
                             ctx->index.presets[ctx->preset_index].name :
                             "No preset";

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 27, preset);
    char temp[12];
    snprintf(temp, sizeof(temp), "%u C", ctx->temp);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 126, 38, AlignRight, AlignBottom, temp);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 46, ctx->status);
    canvas_draw_str(canvas, 2, 62, "< > preset");
    canvas_draw_str_aligned(canvas, 126, 62, AlignRight, AlignBottom, "OK send");
}

static void xremote_ac_draw_vertical(Canvas* canvas, XRemoteAcContext* ctx) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, "AC Smart");
    canvas_draw_str(canvas, 2, 21, ctx->title);
    canvas_draw_line(canvas, 0, 24, 63, 24);

    const char* preset = ctx->index.preset_count ?
                             ctx->index.presets[ctx->preset_index].name :
                             "No preset";

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 41, preset);
    char temp[12];
    snprintf(temp, sizeof(temp), "%u C", ctx->temp);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str(canvas, 2, 65, temp);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 84, ctx->status);
    canvas_draw_str(canvas, 2, 113, "< > preset");
    canvas_draw_str(canvas, 2, 126, "OK send");
}

static void xremote_ac_draw_creator_horizontal(Canvas* canvas, XRemoteAcContext* ctx) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 9, "Create Smart AC");
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, ctx->title);
    canvas_draw_line(canvas, 0, 12, 127, 12);

    canvas_set_font(canvas, FontPrimary);
    if(ctx->create_step == XRemoteAcCreateCaptureOff) {
        canvas_draw_str(canvas, 2, 28, "Capture Off");
    } else if(ctx->create_step == XRemoteAcCreateCaptureTemp) {
        char step[40];
        snprintf(step, sizeof(step), "%s %u C", ctx->preset_name, ctx->sweep_temp);
        canvas_draw_str(canvas, 2, 28, step);
    } else {
        canvas_draw_str(canvas, 2, 28, "Preparing");
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 43, ctx->status);
    canvas_draw_str(canvas, 2, 55, ctx->capture_ready ? "OK save  < retry" : "Point remote at IR");
    if(ctx->create_step == XRemoteAcCreateCaptureTemp) {
        canvas_draw_str_aligned(canvas, 126, 62, AlignRight, AlignBottom, "> finish");
    }
}

static void xremote_ac_draw_creator_vertical(Canvas* canvas, XRemoteAcContext* ctx) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, "Create Smart AC");
    canvas_draw_str(canvas, 2, 21, ctx->title);
    canvas_draw_line(canvas, 0, 24, 63, 24);

    canvas_set_font(canvas, FontPrimary);
    if(ctx->create_step == XRemoteAcCreateCaptureOff) {
        canvas_draw_str(canvas, 2, 42, "Capture Off");
    } else if(ctx->create_step == XRemoteAcCreateCaptureTemp) {
        char step[32];
        snprintf(step, sizeof(step), "%s", ctx->preset_name);
        canvas_draw_str(canvas, 2, 42, step);
        snprintf(step, sizeof(step), "%u C", ctx->sweep_temp);
        canvas_draw_str(canvas, 2, 58, step);
    } else {
        canvas_draw_str(canvas, 2, 42, "Preparing");
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 78, ctx->status);
    canvas_draw_str(canvas, 2, 103, ctx->capture_ready ? "OK save" : "Point IR");
    canvas_draw_str(canvas, 2, 115, "< retry");
    if(ctx->create_step == XRemoteAcCreateCaptureTemp) {
        canvas_draw_str(canvas, 2, 127, "> finish");
    }
}

static void xremote_ac_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    XRemoteViewModel* model = context;
    XRemoteAcContext* ctx = model->context;

    if(ctx->creator_active) {
        if(ctx->app_ctx->app_settings->orientation == ViewOrientationVertical) {
            xremote_ac_draw_creator_vertical(canvas, ctx);
        } else {
            xremote_ac_draw_creator_horizontal(canvas, ctx);
        }
        return;
    }

    if(ctx->app_ctx->app_settings->orientation == ViewOrientationVertical) {
        xremote_ac_draw_vertical(canvas, ctx);
    } else {
        xremote_ac_draw_horizontal(canvas, ctx);
    }
}

static bool xremote_ac_process_creator_input(XRemoteAcContext* ctx, InputEvent* event) {
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyOk) {
            xremote_ac_save_capture(ctx);
            return true;
        } else if(event->key == InputKeyLeft) {
            if(ctx->create_step == XRemoteAcCreateCaptureOff ||
               ctx->create_step == XRemoteAcCreateCaptureTemp) {
                xremote_ac_start_capture(ctx, ctx->create_step);
            }
            return true;
        } else if(event->key == InputKeyRight) {
            if(ctx->create_step == XRemoteAcCreateCaptureTemp) {
                xremote_ac_stop_capture(ctx);
                xremote_ac_store_profile(ctx);
            }
            return true;
        } else if(event->key == InputKeyUp && ctx->create_step == XRemoteAcCreateCaptureTemp) {
            if(ctx->sweep_temp < XREMOTE_AC_SWEEP_TEMP_MAX) ctx->sweep_temp++;
            char status[XREMOTE_AC_STATUS_LEN];
            snprintf(status, sizeof(status), "Temp %u C", ctx->sweep_temp);
            xremote_ac_set_status(ctx, status);
            return true;
        } else if(event->key == InputKeyDown && ctx->create_step == XRemoteAcCreateCaptureTemp) {
            if(ctx->sweep_temp > XREMOTE_AC_SWEEP_TEMP_MIN) ctx->sweep_temp--;
            char status[XREMOTE_AC_STATUS_LEN];
            snprintf(status, sizeof(status), "Temp %u C", ctx->sweep_temp);
            xremote_ac_set_status(ctx, status);
            return true;
        }
    }

    return false;
}

static void xremote_ac_process_input(XRemoteView* view, InputEvent* event) {
    with_view_model(
        xremote_view_get_view(view),
        XRemoteViewModel * model,
        {
            XRemoteAcContext* ctx = xremote_view_get_context(view);
            model->context = ctx;

            if(ctx->creator_active && xremote_ac_process_creator_input(ctx, event)) {
                return;
            }

            if(event->type == InputTypeShort) {
                if(event->key == InputKeyOk) {
                    ctx->ok_pressed = true;
                    xremote_ac_send_current(ctx);
                } else if(event->key == InputKeyUp) {
                    ctx->up_pressed = true;
                    xremote_ac_select_temp(ctx, true);
                } else if(event->key == InputKeyDown) {
                    ctx->down_pressed = true;
                    xremote_ac_select_temp(ctx, false);
                } else if(event->key == InputKeyLeft) {
                    ctx->left_pressed = true;
                    xremote_ac_select_preset(ctx, false);
                } else if(event->key == InputKeyRight) {
                    ctx->right_pressed = true;
                    xremote_ac_select_preset(ctx, true);
                }
            } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
                ctx->hold = true;
                ctx->ok_pressed = true;
                xremote_ac_send_off(ctx);
            } else if(event->type == InputTypeRelease) {
                if(event->key == InputKeyOk) ctx->ok_pressed = false;
                if(event->key == InputKeyUp) ctx->up_pressed = false;
                if(event->key == InputKeyDown) ctx->down_pressed = false;
                if(event->key == InputKeyLeft) ctx->left_pressed = false;
                if(event->key == InputKeyRight) ctx->right_pressed = false;
                ctx->hold = false;
            }
        },
        true);
}

static bool xremote_ac_input_callback(InputEvent* event, void* context) {
    furi_assert(context);
    XRemoteView* view = context;
    XRemoteAppContext* app_ctx = xremote_view_get_app_context(view);
    XRemoteAcContext* ctx = xremote_view_get_context(view);
    XRemoteAppExit exit_behavior = app_ctx->app_settings->exit_behavior;

    if(event->key == InputKeyBack) {
        if(ctx && ctx->creator_active &&
           ((event->type == InputTypeShort && exit_behavior == XRemoteAppExitPress) ||
            (event->type == InputTypeLong && exit_behavior == XRemoteAppExitHold))) {
            xremote_ac_cancel_create(ctx);
            view_dispatcher_switch_to_view(app_ctx->view_dispatcher, XRemoteViewAcSmart);
            return true;
        }

        if(event->type == InputTypeShort && exit_behavior == XRemoteAppExitPress) return false;
        if(event->type == InputTypeLong && exit_behavior == XRemoteAppExitHold) return false;
    }

    xremote_ac_process_input(view, event);
    return true;
}

static XRemoteView* xremote_ac_view_alloc(void* app_ctx, void* model_ctx) {
    XRemoteView* view = xremote_view_alloc(app_ctx, xremote_ac_input_callback, xremote_ac_draw_callback);
    xremote_view_set_context(view, model_ctx, NULL);
    xremote_view_model_context_set(view, model_ctx);
    return view;
}

static void xremote_ac_context_free(void* context) {
    XRemoteAcContext* ctx = context;
    xremote_app_assert_void(ctx);
    xremote_ac_stop_capture(ctx);
    ViewDispatcher* view_disp = ctx->app_ctx->view_dispatcher;
    view_dispatcher_remove_view(view_disp, XRemoteViewTextInput);
    xremote_signal_receiver_free(ctx->ir_receiver);
    infrared_signal_free(ctx->capture_signal);
    infrared_remote_free(ctx->output_remote);
    text_input_free(ctx->text_input);
    if(ctx->file_path) furi_string_free(ctx->file_path);
    furi_record_close(RECORD_STORAGE);
    free(ctx);
}

static XRemoteAcContext* xremote_ac_context_alloc(XRemoteAppContext* app_ctx) {
    XRemoteAcContext* ctx = malloc(sizeof(XRemoteAcContext));
    memset(ctx, 0, sizeof(XRemoteAcContext));
    ctx->app_ctx = app_ctx;
    ctx->storage = furi_record_open(RECORD_STORAGE);
    ctx->text_input = text_input_alloc();
    text_input_set_validator(ctx->text_input, NULL, ctx);
    View* text_view = text_input_get_view(ctx->text_input);
    view_set_previous_callback(text_view, xremote_ac_text_input_exit_callback);
    view_dispatcher_add_view(app_ctx->view_dispatcher, XRemoteViewTextInput, text_view);
    ctx->ir_receiver = xremote_signal_receiver_alloc(app_ctx);
    xremote_signal_receiver_set_context(ctx->ir_receiver, ctx, NULL);
    xremote_signal_receiver_set_rx_callback(ctx->ir_receiver, xremote_ac_rx_callback);
    ctx->capture_signal = infrared_signal_alloc();
    ctx->output_remote = infrared_remote_alloc();
    xremote_ac_prepare_storage(ctx->storage);
    snprintf(ctx->title, sizeof(ctx->title), "No profile");
    xremote_ac_set_status(ctx, "Open profile");
    return ctx;
}

static void xremote_ac_submenu_callback(void* context, uint32_t index) {
    XRemoteApp* app = context;
    xremote_app_assert_void(app);
    XRemoteAcContext* ctx = app->context;
    xremote_app_assert_void(ctx);

    if(index == XRemoteAcMenuCreate) {
        xremote_app_view_alloc2(app, XRemoteViewAcSmartRemote, xremote_ac_view_alloc, ctx);
        xremote_app_view_set_previous_callback(app, xremote_ac_remote_exit_callback);
        xremote_ac_start_name_input(ctx);
        return;
    }

    const char* base_path = index == XRemoteAcMenuOpenFzAc ? XREMOTE_AC_FZ_AC_DIR : XREMOTE_AC_DIR;
    if(!xremote_ac_select_file(ctx, base_path)) return;

    xremote_ac_load_profile(ctx);
    xremote_app_view_alloc2(app, XRemoteViewAcSmartRemote, xremote_ac_view_alloc, ctx);
    xremote_app_view_set_previous_callback(app, xremote_ac_remote_exit_callback);
    xremote_app_switch_to_view(app, XRemoteViewAcSmartRemote);
}

XRemoteApp* xremote_ac_alloc(XRemoteAppContext* app_ctx) {
    XRemoteApp* app = xremote_app_alloc(app_ctx);
    XRemoteAcContext* ctx = xremote_ac_context_alloc(app_ctx);
    xremote_app_set_user_context(app, ctx, xremote_ac_context_free);
    xremote_app_submenu_alloc(app, XRemoteViewAcSmart, xremote_ac_submenu_exit_callback);
    xremote_app_submenu_add(
        app, "Create Smart AC", XRemoteAcMenuCreate, xremote_ac_submenu_callback);
    xremote_app_submenu_add(app, "Open Tumo AC", XRemoteAcMenuOpenTumo, xremote_ac_submenu_callback);
    xremote_app_submenu_add(
        app, "Open fz-ac profile", XRemoteAcMenuOpenFzAc, xremote_ac_submenu_callback);
    return app;
}
