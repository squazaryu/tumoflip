/*!
 *  @file flipper-xremote/xremote_ac.c
 *  @license This project is released under the GNU GPLv3 License
 *
 * @brief Smart A/C controller integrated into Tumo XRemote.
 */

#include "xremote_ac.h"
#include "xremote_ac_engine.h"

#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <toolbox/path.h>

#define XREMOTE_AC_STATUS_LEN  40
#define XREMOTE_AC_TITLE_LEN   24

typedef enum {
    XRemoteAcMenuOpenTumo,
    XRemoteAcMenuOpenFzAc,
} XRemoteAcMenu;

typedef struct {
    XRemoteAppContext* app_ctx;
    Storage* storage;
    FuriString* file_path;
    XRemoteAcIndex index;
    char title[XREMOTE_AC_TITLE_LEN];
    char status[XREMOTE_AC_STATUS_LEN];
    uint8_t preset_index;
    uint8_t temp;
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

static void xremote_ac_draw_callback(Canvas* canvas, void* context) {
    furi_assert(context);
    XRemoteViewModel* model = context;
    XRemoteAcContext* ctx = model->context;

    if(ctx->app_ctx->app_settings->orientation == ViewOrientationVertical) {
        xremote_ac_draw_vertical(canvas, ctx);
    } else {
        xremote_ac_draw_horizontal(canvas, ctx);
    }
}

static void xremote_ac_process_input(XRemoteView* view, InputEvent* event) {
    with_view_model(
        xremote_view_get_view(view),
        XRemoteViewModel * model,
        {
            XRemoteAcContext* ctx = xremote_view_get_context(view);
            model->context = ctx;

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
    XRemoteAppExit exit_behavior = app_ctx->app_settings->exit_behavior;

    if(event->key == InputKeyBack) {
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
    if(ctx->file_path) furi_string_free(ctx->file_path);
    furi_record_close(RECORD_STORAGE);
    free(ctx);
}

static XRemoteAcContext* xremote_ac_context_alloc(XRemoteAppContext* app_ctx) {
    XRemoteAcContext* ctx = malloc(sizeof(XRemoteAcContext));
    memset(ctx, 0, sizeof(XRemoteAcContext));
    ctx->app_ctx = app_ctx;
    ctx->storage = furi_record_open(RECORD_STORAGE);
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
    xremote_app_submenu_add(app, "Open Tumo AC", XRemoteAcMenuOpenTumo, xremote_ac_submenu_callback);
    xremote_app_submenu_add(
        app, "Open fz-ac profile", XRemoteAcMenuOpenFzAc, xremote_ac_submenu_callback);
    return app;
}
