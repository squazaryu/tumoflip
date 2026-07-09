/*!
 * @file flipper-xremote/xremote_ac.c
 * @license GPLv3 for Tumo XRemote integration.
 *
 * Smart AC integration based on fz-ac by Cengiz Ozel (MIT).
 * Button panel and AC icon assets include portions from
 * flipperzero-mitsubishi-ac-remote by Anton Chistyakov (MIT).
 */

#include "xremote_ac.h"

#include "ac_smart/ac_ir.h"
#include "ac_smart/views/ac_remote_panel.h"
#include "ac_smart/views/learn_view.h"
#include "ac_smart/views/sweep_view.h"

#include <ctype.h>
#include <gui/modules/dialog_ex.h>
#include <infrared_transmit.h>
#include <storage/storage.h>
#include <toolbox/path.h>

#define XREMOTE_AC_DIR      EXT_PATH("apps_data/fz_ac")
#define XREMOTE_AC_MAX_ACS  32
#define XREMOTE_AC_NAME_LEN 24
#define XREMOTE_AC_PATH_LEN 128
#define XREMOTE_AC_BUTTON_REQUIRED_COUNT 4

typedef enum {
    XRemoteAcMenuAddSmart = 1,
    XRemoteAcMenuAddSimple,
    XRemoteAcMenuOpenBase = 100,
} XRemoteAcMenu;

typedef enum {
    XRemoteAcFlowIdle,
    XRemoteAcFlowSimpleLearn,
    XRemoteAcFlowSmartOff,
    XRemoteAcFlowSmartSweep,
} XRemoteAcFlow;

typedef enum {
    XRemoteAcEventIrCaptured = 0xAC01,
} XRemoteAcEvent;

enum {
    RemoteLabelTitle = 100,
    RemoteLabelTemperature,
    RemoteLabelPreset,
    RemoteLabelPresetPos,
};

typedef struct {
    const Icon* icon;
    const Icon* icon_hover;
    uint16_t matrix_x;
    uint16_t matrix_y;
    uint16_t x;
    uint16_t y;
} XRemoteAcButtonLayout;

static const XRemoteAcButtonLayout remote_layout[AC_BUTTON_COUNT] = {
    [AcButtonOff] = {&I_ac_off_19x20, &I_ac_off_hover_19x20, 0, 0, 6, 17},
    [AcButtonMode] = {&I_cold_19x20, &I_cold_hover_19x20, 1, 0, 39, 17},
    [AcButtonTempUp] = {&I_tempup_24x21, &I_tempup_hover_24x21, 0, 1, 3, 51},
    [AcButtonTempDown] = {&I_tempdown_24x21, &I_tempdown_hover_24x21, 0, 2, 3, 93},
    [AcButtonFan] = {&I_fan_silent_19x20, &I_fan_silent_hover_19x20, 1, 1, 39, 54},
    [AcButtonVane] = {&I_vane_h3_19x20, &I_vane_h3_hover_19x20, 1, 2, 39, 91},
};

typedef struct {
    XRemoteApp* app;
    XRemoteAppContext* app_ctx;
    Storage* storage;
    TextInput* text_input;
    DialogEx* delete_dialog;
    ACRemotePanel* panel;
    LearnView* learn_view;
    SweepView* sweep_view;

    char ac_names[XREMOTE_AC_MAX_ACS][XREMOTE_AC_NAME_LEN];
    uint32_t ac_count;
    int32_t current_ac;
    AcType current_type;
    AcRemote remote;
    AcSmartIndex smart_index;
    uint8_t smart_preset;
    uint8_t smart_temp;
    uint32_t temp_display;
    char preset_label[AC_PRESET_NAME_LEN];
    char preset_pos[8];
    char temp_str[8];
    char title_buf[12];

    XRemoteAcFlow flow;
    char name_buf[XREMOTE_AC_NAME_LEN];
    char learn_target[XREMOTE_AC_NAME_LEN];
    uint8_t learn_index;
    InfraredWorker* rx_worker;
    bool rx_active;
    FuriMutex* signal_mutex;
    FuriString* str;
    AcIrSignal capture;
    AcRemote staged;
    int32_t delete_ac;
    char delete_name[XREMOTE_AC_NAME_LEN];

    AcIrSignal off_capture;
    char preset_buf[AC_PRESET_NAME_LEN];
    AcIrSignal sweep[AC_SWEEP_MAX];
    uint8_t sweep_count;
    uint8_t sweep_temp_start;
    uint32_t current_view;
} XRemoteAcContext;

static void xremote_ac_show_menu(XRemoteAcContext* ctx);
static void xremote_ac_open_remote(XRemoteAcContext* ctx);
static void xremote_ac_start_name_input(XRemoteAcContext* ctx, XRemoteAcFlow flow);
static void xremote_ac_start_preset_input(XRemoteAcContext* ctx);
static void xremote_ac_start_simple_learn(XRemoteAcContext* ctx);
static void xremote_ac_start_smart_off(XRemoteAcContext* ctx);
static void xremote_ac_start_sweep(XRemoteAcContext* ctx);
static void xremote_ac_submenu_callback(void* context, uint32_t index);
static void xremote_ac_submenu_callback_ex(void* context, InputType input_type, uint32_t index);
static void xremote_ac_delete_dialog_callback(DialogExResult result, void* context);
static void xremote_ac_remote_button_callback(void* context, uint32_t index);
static void xremote_ac_learn_view_callback(void* context, LearnViewEvent event);
static void xremote_ac_sweep_view_callback(void* context, SweepViewEvent event);

static void xremote_ac_switch_to_view(XRemoteAcContext* ctx, uint32_t view_id) {
    ctx->current_view = view_id;
    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, view_id);
}

static uint32_t xremote_ac_submenu_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewSubmenu;
}

static uint32_t xremote_ac_delete_dialog_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewAcSmart;
}

static void xremote_ac_ac_path(const char* name, char* out, size_t out_size) {
    snprintf(out, out_size, "%s/%s.ir", XREMOTE_AC_DIR, name);
}

static void xremote_ac_sanitize_name(char* name) {
    char* out = name;
    for(const char* in = name; *in; in++) {
        const unsigned char c = (unsigned char)*in;
        if(isalnum(c) || c == ' ' || c == '-' || c == '_') {
            *out++ = (char)c;
        }
    }
    *out = '\0';

    while(name[0] == ' ') memmove(name, name + 1, strlen(name));
    size_t len = strlen(name);
    while(len > 0 && name[len - 1] == ' ') name[--len] = '\0';
    if(name[0] == '\0') snprintf(name, XREMOTE_AC_NAME_LEN, "AC");
}

static void xremote_ac_make_unique_name(XRemoteAcContext* ctx, char* name, size_t size) {
    char path[XREMOTE_AC_PATH_LEN];
    xremote_ac_ac_path(name, path, sizeof(path));
    if(!storage_file_exists(ctx->storage, path)) return;

    char base[XREMOTE_AC_NAME_LEN];
    snprintf(base, sizeof(base), "%s", name);
    size_t max_base = (size > 4) ? size - 4 : 0;
    if(strlen(base) > max_base) base[max_base] = '\0';

    for(uint8_t n = 2; n < 100; n++) {
        snprintf(name, size, "%.*s %u", (int)max_base, base, n);
        xremote_ac_ac_path(name, path, sizeof(path));
        if(!storage_file_exists(ctx->storage, path)) return;
    }
}

static int32_t xremote_ac_find_ac(XRemoteAcContext* ctx, const char* name) {
    for(uint32_t i = 0; i < ctx->ac_count; i++) {
        if(strcmp(ctx->ac_names[i], name) == 0) return i;
    }
    return -1;
}

static void xremote_ac_sort_names(XRemoteAcContext* ctx) {
    char tmp[XREMOTE_AC_NAME_LEN];
    for(uint32_t i = 1; i < ctx->ac_count; i++) {
        memcpy(tmp, ctx->ac_names[i], sizeof(tmp));
        int32_t j = (int32_t)i - 1;
        while(j >= 0 && strcasecmp(ctx->ac_names[j], tmp) > 0) {
            memcpy(ctx->ac_names[j + 1], ctx->ac_names[j], XREMOTE_AC_NAME_LEN);
            j--;
        }
        memcpy(ctx->ac_names[j + 1], tmp, XREMOTE_AC_NAME_LEN);
    }
}

static void xremote_ac_refresh_list(XRemoteAcContext* ctx) {
    ctx->ac_count = 0;
    storage_simply_mkdir(ctx->storage, XREMOTE_AC_DIR);

    File* dir = storage_file_alloc(ctx->storage);
    if(storage_dir_open(dir, XREMOTE_AC_DIR)) {
        FileInfo info;
        char name[XREMOTE_AC_PATH_LEN];
        while(ctx->ac_count < XREMOTE_AC_MAX_ACS &&
              storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            size_t len = strlen(name);
            if(len < 4 || strcmp(&name[len - 3], ".ir") != 0) continue;
            name[len - 3] = '\0';
            if(strlen(name) >= XREMOTE_AC_NAME_LEN) continue;
            snprintf(
                ctx->ac_names[ctx->ac_count],
                XREMOTE_AC_NAME_LEN,
                "%.*s",
                XREMOTE_AC_NAME_LEN - 1,
                name);
            ctx->ac_count++;
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
    xremote_ac_sort_names(ctx);
}

static void xremote_ac_rebuild_menu(XRemoteAcContext* ctx) {
    submenu_reset(ctx->app->submenu);
    xremote_app_submenu_add(
        ctx->app, "Add Smart AC", XRemoteAcMenuAddSmart, xremote_ac_submenu_callback);
    xremote_app_submenu_add(
        ctx->app, "Add Button AC", XRemoteAcMenuAddSimple, xremote_ac_submenu_callback);

    xremote_ac_refresh_list(ctx);
    for(uint32_t i = 0; i < ctx->ac_count; i++) {
        submenu_add_item_ex(
            ctx->app->submenu,
            ctx->ac_names[i],
            XRemoteAcMenuOpenBase + i,
            xremote_ac_submenu_callback_ex,
            ctx);
    }
}

static void xremote_ac_load_current(XRemoteAcContext* ctx) {
    ac_remote_reset(&ctx->remote);
    memset(&ctx->smart_index, 0, sizeof(ctx->smart_index));
    ctx->current_type = AcTypeSimple;
    if(ctx->current_ac < 0 || ctx->current_ac >= (int32_t)ctx->ac_count) return;

    char path[XREMOTE_AC_PATH_LEN];
    xremote_ac_ac_path(ctx->ac_names[ctx->current_ac], path, sizeof(path));
    ctx->current_type = ac_file_scan(ctx->storage, path, &ctx->smart_index);
    if(ctx->current_type == AcTypeSimple) {
        ac_remote_load(&ctx->remote, ctx->storage, path);
        ctx->temp_display = 20;
    } else {
        ctx->smart_preset = 0;
        int32_t temp = -1;
        if(ctx->smart_index.preset_count > 0) {
            temp = ac_temp_bits_lowest(ctx->smart_index.presets[0].temp_bits);
        }
        ctx->smart_temp = (temp > 0) ? temp : 22;
    }
}

static void xremote_ac_rx_callback(void* context, InfraredWorkerSignal* signal) {
    XRemoteAcContext* ctx = context;
    furi_mutex_acquire(ctx->signal_mutex, FuriWaitForever);
    ac_ir_signal_from_worker(&ctx->capture, signal);
    furi_mutex_release(ctx->signal_mutex);
    view_dispatcher_send_custom_event(ctx->app_ctx->view_dispatcher, XRemoteAcEventIrCaptured);
}

static void xremote_ac_rx_alloc(XRemoteAcContext* ctx) {
    if(ctx->rx_worker) return;
    ctx->rx_worker = infrared_worker_alloc();
    infrared_worker_rx_set_received_signal_callback(ctx->rx_worker, xremote_ac_rx_callback, ctx);
    infrared_worker_rx_enable_signal_decoding(ctx->rx_worker, true);
    infrared_worker_rx_enable_blink_on_receiving(ctx->rx_worker, true);
}

static void xremote_ac_rx_stop(XRemoteAcContext* ctx) {
    if(ctx->rx_worker && ctx->rx_active) {
        infrared_worker_rx_stop(ctx->rx_worker);
        ctx->rx_active = false;
    }
}

static void xremote_ac_rx_free(XRemoteAcContext* ctx) {
    if(!ctx->rx_worker) return;
    xremote_ac_rx_stop(ctx);
    infrared_worker_free(ctx->rx_worker);
    ctx->rx_worker = NULL;
    furi_mutex_acquire(ctx->signal_mutex, FuriWaitForever);
    ac_ir_signal_reset(&ctx->capture);
    furi_mutex_release(ctx->signal_mutex);
}

static void xremote_ac_rx_start(XRemoteAcContext* ctx) {
    xremote_ac_rx_alloc(ctx);
    if(ctx->rx_worker && !ctx->rx_active) {
        infrared_worker_rx_start(ctx->rx_worker);
        ctx->rx_active = true;
    }
}

static void xremote_ac_cancel_flow(XRemoteAcContext* ctx) {
    xremote_ac_rx_stop(ctx);
    ctx->flow = XRemoteAcFlowIdle;
    ac_remote_reset(&ctx->staged);
    ac_ir_signal_reset(&ctx->capture);
    ac_ir_signal_reset(&ctx->off_capture);
    for(size_t i = 0; i < AC_SWEEP_MAX; i++) ac_ir_signal_reset(&ctx->sweep[i]);
    ctx->sweep_count = 0;
}

static void xremote_ac_show_menu(XRemoteAcContext* ctx) {
    xremote_ac_cancel_flow(ctx);
    xremote_ac_rebuild_menu(ctx);
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmart);
}

static void xremote_ac_notify(XRemoteAcContext* ctx, bool success) {
    notification_message(ctx->app_ctx->notifications, success ? &sequence_success : &sequence_error);
}

static void xremote_ac_send_ir(XRemoteAcContext* ctx, AcIrSignal* signal) {
    if(signal->present) {
        notification_message(ctx->app_ctx->notifications, &sequence_blink_white_100);
        ac_ir_signal_send(signal);
    } else {
        notification_message(ctx->app_ctx->notifications, &sequence_blink_red_100);
    }
}

static bool xremote_ac_load_signal(
    XRemoteAcContext* ctx,
    AcIrSignal* signal,
    const char* preset,
    uint8_t temp) {
    char path[XREMOTE_AC_PATH_LEN];
    char name[AC_PRESET_NAME_LEN + 8];
    xremote_ac_ac_path(ctx->ac_names[ctx->current_ac], path, sizeof(path));
    if(preset) {
        ac_smart_signal_name(name, sizeof(name), preset, temp);
    } else {
        snprintf(name, sizeof(name), "%s", AC_OFF_NAME);
    }
    return ac_file_load_signal(signal, ctx->storage, path, name);
}

static void xremote_ac_smart_send(XRemoteAcContext* ctx, const char* preset, uint8_t temp) {
    AcIrSignal signal;
    memset(&signal, 0, sizeof(signal));
    const bool loaded = xremote_ac_load_signal(ctx, &signal, preset, temp);
    xremote_ac_send_ir(ctx, loaded ? &signal : &signal);
    ac_ir_signal_reset(&signal);
}

static void xremote_ac_update_smart_labels(XRemoteAcContext* ctx) {
    if(ctx->smart_index.preset_count > 0) {
        const AcPresetInfo* preset = &ctx->smart_index.presets[ctx->smart_preset];
        snprintf(ctx->preset_label, sizeof(ctx->preset_label), "%.6s", preset->name);
        snprintf(
            ctx->preset_pos,
            sizeof(ctx->preset_pos),
            "%u/%u",
            ctx->smart_preset + 1,
            ctx->smart_index.preset_count);
        snprintf(ctx->temp_str, sizeof(ctx->temp_str), "%u", ctx->smart_temp);
    } else {
        snprintf(ctx->preset_label, sizeof(ctx->preset_label), "none");
        ctx->preset_pos[0] = '\0';
        snprintf(ctx->temp_str, sizeof(ctx->temp_str), "--");
    }
}

static void xremote_ac_remote_add_button(XRemoteAcContext* ctx, AcButton button) {
    const XRemoteAcButtonLayout* layout = &remote_layout[button];
    ac_remote_panel_add_item(
        ctx->panel,
        button,
        layout->matrix_x,
        layout->matrix_y,
        layout->x,
        layout->y,
        layout->icon,
        layout->icon_hover,
        xremote_ac_remote_button_callback,
        ctx);
}

static void xremote_ac_panel_refresh(XRemoteAcContext* ctx) {
    ACRemotePanel* panel = ctx->panel;
    const bool smart = ctx->current_type == AcTypeSmart;
    const bool has_fan = !smart && ctx->remote.signals[AcButtonFan].present;
    const bool has_vane = !smart && ctx->remote.signals[AcButtonVane].present;

    ac_remote_panel_reset(panel);
    ac_remote_panel_reserve(panel, 2, 3);
    xremote_ac_remote_add_button(ctx, AcButtonOff);
    xremote_ac_remote_add_button(ctx, AcButtonMode);
    xremote_ac_remote_add_button(ctx, AcButtonTempUp);
    xremote_ac_remote_add_button(ctx, AcButtonTempDown);
    if(has_fan) {
        xremote_ac_remote_add_button(ctx, AcButtonFan);
    }
    if(has_vane) {
        xremote_ac_remote_add_button(ctx, AcButtonVane);
    }

    ac_remote_panel_add_icon(panel, 9, 39, &I_off_text_14x5);
    ac_remote_panel_add_icon(panel, 39, 39, &I_ac_mode_text_20x5);
    ac_remote_panel_add_icon(panel, 0, 63, &I_frame_30x39);
    if(has_fan) {
        ac_remote_panel_add_icon(panel, 41, 76, &I_fan_text_14x5);
    }
    if(has_vane) {
        ac_remote_panel_add_icon(panel, 38, 113, &I_vane_text_20x5);
    }

    if(ctx->current_ac >= 0 && ctx->current_ac < (int32_t)ctx->ac_count) {
        snprintf(ctx->title_buf, sizeof(ctx->title_buf), "%s", ctx->ac_names[ctx->current_ac]);
    } else {
        snprintf(ctx->title_buf, sizeof(ctx->title_buf), "AC remote");
    }
    ac_remote_panel_add_label(panel, RemoteLabelTitle, 2, 11, FontPrimary, ctx->title_buf);

    if(smart) {
        xremote_ac_update_smart_labels(ctx);
        ac_remote_panel_add_label_centered(
            panel, RemoteLabelPreset, 48, 53, FontSecondary, ctx->preset_label);
        ac_remote_panel_add_label_centered(
            panel, RemoteLabelPresetPos, 48, 64, FontSecondary, ctx->preset_pos);
    } else {
        snprintf(ctx->temp_str, sizeof(ctx->temp_str), "%lu", ctx->temp_display);
    }
    ac_remote_panel_add_label(panel, RemoteLabelTemperature, 4, 86, FontKeyboard, ctx->temp_str);
}

static void xremote_ac_open_remote(XRemoteAcContext* ctx) {
    xremote_ac_load_current(ctx);
    xremote_ac_panel_refresh(ctx);
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmartRemote);
}

static void xremote_ac_remote_simple_press(XRemoteAcContext* ctx, uint32_t button) {
    if(button >= AC_BUTTON_COUNT) return;
    AcIrSignal* signal = &ctx->remote.signals[button];
    xremote_ac_send_ir(ctx, signal);
    if(!signal->present) return;

    if(button == AcButtonTempUp && ctx->temp_display < 31) {
        ctx->temp_display++;
    } else if(button == AcButtonTempDown && ctx->temp_display > 16) {
        ctx->temp_display--;
    }
    snprintf(ctx->temp_str, sizeof(ctx->temp_str), "%lu", ctx->temp_display);
    ac_remote_panel_label_set_string(ctx->panel, RemoteLabelTemperature, ctx->temp_str);
}

static void xremote_ac_remote_smart_press(XRemoteAcContext* ctx, uint32_t button) {
    if(button == AcButtonOff) {
        xremote_ac_smart_send(ctx, NULL, 0);
        return;
    }
    if(ctx->smart_index.preset_count == 0) {
        notification_message(ctx->app_ctx->notifications, &sequence_blink_red_100);
        return;
    }

    AcPresetInfo* preset = &ctx->smart_index.presets[ctx->smart_preset];
    if(button == AcButtonMode) {
        ctx->smart_preset = (ctx->smart_preset + 1) % ctx->smart_index.preset_count;
        preset = &ctx->smart_index.presets[ctx->smart_preset];
        int32_t temp = ac_temp_bits_nearest(preset->temp_bits, ctx->smart_temp);
        if(temp < 0) {
            notification_message(ctx->app_ctx->notifications, &sequence_blink_red_100);
            return;
        }
        ctx->smart_temp = temp;
    } else if(button == AcButtonTempUp || button == AcButtonTempDown) {
        int32_t temp =
            ac_temp_bits_next(preset->temp_bits, ctx->smart_temp, button == AcButtonTempUp);
        if(temp < 0) {
            notification_message(ctx->app_ctx->notifications, &sequence_blink_red_100);
            return;
        }
        ctx->smart_temp = temp;
    } else {
        return;
    }

    xremote_ac_update_smart_labels(ctx);
    ac_remote_panel_label_set_string(ctx->panel, RemoteLabelPreset, ctx->preset_label);
    ac_remote_panel_label_set_string(ctx->panel, RemoteLabelPresetPos, ctx->preset_pos);
    ac_remote_panel_label_set_string(ctx->panel, RemoteLabelTemperature, ctx->temp_str);
    xremote_ac_smart_send(ctx, preset->name, ctx->smart_temp);
}

static void xremote_ac_remote_button_callback(void* context, uint32_t index) {
    XRemoteAcContext* ctx = context;
    if(ctx->current_type == AcTypeSmart) {
        xremote_ac_remote_smart_press(ctx, index);
    } else {
        xremote_ac_remote_simple_press(ctx, index);
    }
}

static void xremote_ac_show_learn_current(XRemoteAcContext* ctx) {
    const char* label = "OFF";
    uint8_t index = 1;
    uint8_t total = 1;
    if(ctx->flow == XRemoteAcFlowSimpleLearn) {
        label = ac_button_labels[ctx->learn_index];
        index = ctx->learn_index + 1;
        total = XREMOTE_AC_BUTTON_REQUIRED_COUNT;
    }

    learn_view_set_button(ctx->learn_view, label, index, total);
    learn_view_set_captured(ctx->learn_view, false, "");
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmartLearn);
}

static void xremote_ac_start_simple_learn(XRemoteAcContext* ctx) {
    ctx->flow = XRemoteAcFlowSimpleLearn;
    ctx->learn_index = 0;
    ac_remote_reset(&ctx->staged);
    ac_ir_signal_reset(&ctx->capture);
    xremote_ac_show_learn_current(ctx);
    xremote_ac_rx_start(ctx);
}

static void xremote_ac_start_smart_off(XRemoteAcContext* ctx) {
    ctx->flow = XRemoteAcFlowSmartOff;
    ac_ir_signal_reset(&ctx->capture);
    ac_ir_signal_reset(&ctx->off_capture);
    xremote_ac_show_learn_current(ctx);
    xremote_ac_rx_start(ctx);
}

static void xremote_ac_simple_finish(XRemoteAcContext* ctx) {
    xremote_ac_rx_stop(ctx);

    bool any_present = false;
    for(size_t i = 0; i < AC_BUTTON_COUNT; i++) {
        if(ctx->staged.signals[i].present) {
            any_present = true;
            break;
        }
    }

    bool saved = false;
    if(any_present) {
        char path[XREMOTE_AC_PATH_LEN];
        xremote_ac_ac_path(ctx->learn_target, path, sizeof(path));
        saved = ac_remote_save(&ctx->staged, ctx->storage, path);
    }

    xremote_ac_notify(ctx, saved);
    if(saved) {
        xremote_ac_refresh_list(ctx);
        ctx->current_ac = xremote_ac_find_ac(ctx, ctx->learn_target);
        xremote_ac_cancel_flow(ctx);
        xremote_ac_open_remote(ctx);
    } else {
        xremote_ac_show_menu(ctx);
    }
}

static void xremote_ac_simple_advance(XRemoteAcContext* ctx) {
    ctx->learn_index++;
    if(ctx->learn_index < XREMOTE_AC_BUTTON_REQUIRED_COUNT) {
        xremote_ac_show_learn_current(ctx);
        xremote_ac_rx_start(ctx);
    } else {
        xremote_ac_simple_finish(ctx);
    }
}

static void xremote_ac_learn_view_callback(void* context, LearnViewEvent event) {
    XRemoteAcContext* ctx = context;

    if(event == LearnViewEventRetry) {
        furi_mutex_acquire(ctx->signal_mutex, FuriWaitForever);
        ac_ir_signal_reset(&ctx->capture);
        furi_mutex_release(ctx->signal_mutex);
        learn_view_set_captured(ctx->learn_view, false, "");
        xremote_ac_rx_start(ctx);
        return;
    }

    if(ctx->flow == XRemoteAcFlowSimpleLearn) {
        if(event == LearnViewEventOk) {
            furi_mutex_acquire(ctx->signal_mutex, FuriWaitForever);
            ac_ir_signal_move(&ctx->staged.signals[ctx->learn_index], &ctx->capture);
            furi_mutex_release(ctx->signal_mutex);
        } else {
            xremote_ac_rx_stop(ctx);
            ac_ir_signal_reset(&ctx->staged.signals[ctx->learn_index]);
        }
        xremote_ac_simple_advance(ctx);
    } else if(ctx->flow == XRemoteAcFlowSmartOff) {
        if(event == LearnViewEventOk) {
            furi_mutex_acquire(ctx->signal_mutex, FuriWaitForever);
            ac_ir_signal_move(&ctx->off_capture, &ctx->capture);
            furi_mutex_release(ctx->signal_mutex);
            xremote_ac_start_preset_input(ctx);
        } else if(event == LearnViewEventSkip) {
            xremote_ac_start_preset_input(ctx);
        }
    }
}

static void xremote_ac_sweep_reset(XRemoteAcContext* ctx) {
    for(size_t i = 0; i < AC_SWEEP_MAX; i++) ac_ir_signal_reset(&ctx->sweep[i]);
    ctx->sweep_count = 0;
}

static void xremote_ac_sweep_update(XRemoteAcContext* ctx) {
    sweep_view_update(ctx->sweep_view, ctx->preset_buf, ctx->sweep_temp_start, ctx->sweep_count);
}

static void xremote_ac_sweep_done(XRemoteAcContext* ctx) {
    if(ctx->sweep_count == 0) {
        notification_message(ctx->app_ctx->notifications, &sequence_blink_red_100);
        return;
    }
    xremote_ac_rx_stop(ctx);

    char path[XREMOTE_AC_PATH_LEN];
    xremote_ac_ac_path(ctx->learn_target, path, sizeof(path));
    const bool saved = ac_smart_write_preset(
        ctx->storage,
        path,
        true,
        &ctx->off_capture,
        ctx->preset_buf,
        ctx->sweep_temp_start,
        ctx->sweep,
        ctx->sweep_count);

    xremote_ac_notify(ctx, saved);
    if(saved) {
        xremote_ac_refresh_list(ctx);
        ctx->current_ac = xremote_ac_find_ac(ctx, ctx->learn_target);
        xremote_ac_cancel_flow(ctx);
        xremote_ac_open_remote(ctx);
    } else {
        xremote_ac_rx_start(ctx);
    }
}

static void xremote_ac_sweep_view_callback(void* context, SweepViewEvent event) {
    XRemoteAcContext* ctx = context;

    if(event == SweepViewEventDone) {
        xremote_ac_sweep_done(ctx);
    } else if(event == SweepViewEventUndo) {
        if(ctx->sweep_count > 0) {
            ctx->sweep_count--;
            ac_ir_signal_reset(&ctx->sweep[ctx->sweep_count]);
            xremote_ac_sweep_update(ctx);
        }
    } else if(event == SweepViewEventUp) {
        if(ctx->sweep_temp_start + ctx->sweep_count < AC_TEMP_BASE + AC_TEMP_SLOTS - 1) {
            ctx->sweep_temp_start++;
            xremote_ac_sweep_update(ctx);
        }
    } else if(event == SweepViewEventDown) {
        if(ctx->sweep_temp_start > AC_TEMP_BASE) {
            ctx->sweep_temp_start--;
            xremote_ac_sweep_update(ctx);
        }
    }
}

static void xremote_ac_start_sweep(XRemoteAcContext* ctx) {
    ctx->flow = XRemoteAcFlowSmartSweep;
    ctx->sweep_temp_start = 16;
    xremote_ac_sweep_reset(ctx);
    xremote_ac_sweep_update(ctx);
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmartSweep);
    xremote_ac_rx_start(ctx);
}

static bool xremote_ac_custom_event_callback(void* context, uint32_t event) {
    XRemoteAcContext* ctx = context;
    if(event != XRemoteAcEventIrCaptured) return false;

    if(ctx->flow == XRemoteAcFlowSimpleLearn || ctx->flow == XRemoteAcFlowSmartOff) {
        xremote_ac_rx_stop(ctx);
        furi_mutex_acquire(ctx->signal_mutex, FuriWaitForever);
        ac_ir_signal_describe(&ctx->capture, ctx->str);
        furi_mutex_release(ctx->signal_mutex);
        learn_view_set_captured(ctx->learn_view, true, furi_string_get_cstr(ctx->str));
        return true;
    }

    if(ctx->flow == XRemoteAcFlowSmartSweep) {
        furi_mutex_acquire(ctx->signal_mutex, FuriWaitForever);
        if(ctx->sweep_count < AC_SWEEP_MAX &&
           ctx->sweep_temp_start + ctx->sweep_count < AC_TEMP_BASE + AC_TEMP_SLOTS) {
            ac_ir_signal_move(&ctx->sweep[ctx->sweep_count], &ctx->capture);
            ctx->sweep_count++;
        } else {
            ac_ir_signal_reset(&ctx->capture);
            notification_message(ctx->app_ctx->notifications, &sequence_blink_red_100);
        }
        furi_mutex_release(ctx->signal_mutex);
        xremote_ac_sweep_update(ctx);
        return true;
    }

    return true;
}

static bool xremote_ac_back_event_callback(void* context) {
    XRemoteAcContext* ctx = context;
    const uint32_t current_view = ctx->current_view;

    if(current_view == XRemoteViewAcSmart) return false;

    if(current_view == XRemoteViewAcSmartTextInput || current_view == XRemoteViewAcSmartLearn ||
       current_view == XRemoteViewAcSmartSweep) {
        xremote_ac_show_menu(ctx);
        return true;
    }

    if(current_view == XRemoteViewAcSmartRemote) {
        xremote_ac_show_menu(ctx);
        return true;
    }

    return false;
}

static uint32_t xremote_ac_text_input_exit_callback(void* context) {
    TextInput* text_input = context;
    XRemoteAcContext* ctx = text_input_get_validator_callback_context(text_input);
    xremote_ac_show_menu(ctx);
    return XRemoteViewAcSmart;
}

static void xremote_ac_name_input_callback(void* context) {
    XRemoteAcContext* ctx = context;
    xremote_ac_sanitize_name(ctx->name_buf);
    xremote_ac_make_unique_name(ctx, ctx->name_buf, sizeof(ctx->name_buf));
    snprintf(ctx->learn_target, sizeof(ctx->learn_target), "%s", ctx->name_buf);

    if(ctx->flow == XRemoteAcFlowSimpleLearn) {
        xremote_ac_start_simple_learn(ctx);
    } else {
        xremote_ac_start_smart_off(ctx);
    }
}

static void xremote_ac_preset_input_callback(void* context) {
    XRemoteAcContext* ctx = context;
    xremote_ac_sanitize_name(ctx->preset_buf);
    if(ctx->preset_buf[0] == '\0') snprintf(ctx->preset_buf, sizeof(ctx->preset_buf), "Cool");
    xremote_ac_start_sweep(ctx);
}

static void xremote_ac_start_name_input(XRemoteAcContext* ctx, XRemoteAcFlow flow) {
    ctx->flow = flow;
    snprintf(ctx->name_buf, sizeof(ctx->name_buf), "AC");
    text_input_reset(ctx->text_input);
    text_input_set_header_text(ctx->text_input, "AC name");
    text_input_set_result_callback(
        ctx->text_input,
        xremote_ac_name_input_callback,
        ctx,
        ctx->name_buf,
        XREMOTE_AC_NAME_LEN - 1,
        true);
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmartTextInput);
}

static void xremote_ac_start_preset_input(XRemoteAcContext* ctx) {
    snprintf(ctx->preset_buf, sizeof(ctx->preset_buf), "Cool");
    text_input_reset(ctx->text_input);
    text_input_set_header_text(ctx->text_input, "Preset");
    text_input_set_result_callback(
        ctx->text_input,
        xremote_ac_preset_input_callback,
        ctx,
        ctx->preset_buf,
        AC_PRESET_NAME_LEN - 1,
        true);
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmartTextInput);
}

static void xremote_ac_open_ac_index(XRemoteAcContext* ctx, uint32_t ac_index) {
    if(ac_index < ctx->ac_count) {
        ctx->current_ac = ac_index;
        xremote_ac_open_remote(ctx);
    }
}

static void xremote_ac_show_delete_dialog(XRemoteAcContext* ctx, uint32_t ac_index) {
    if(ac_index >= ctx->ac_count) {
        xremote_ac_notify(ctx, false);
        return;
    }

    ctx->delete_ac = (int32_t)ac_index;
    snprintf(ctx->delete_name, sizeof(ctx->delete_name), "%s", ctx->ac_names[ac_index]);

    char text[64];
    snprintf(text, sizeof(text), "%s\nfrom Smart AC", ctx->delete_name);

    dialog_ex_reset(ctx->delete_dialog);
    dialog_ex_set_header(ctx->delete_dialog, "Delete AC?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(ctx->delete_dialog, text, 64, 20, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(ctx->delete_dialog, "Cancel");
    dialog_ex_set_right_button_text(ctx->delete_dialog, "Delete");
    dialog_ex_set_context(ctx->delete_dialog, ctx);
    dialog_ex_set_result_callback(ctx->delete_dialog, xremote_ac_delete_dialog_callback);
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmartDelete);
}

static void xremote_ac_delete_pending(XRemoteAcContext* ctx) {
    if(ctx->delete_ac < 0) {
        xremote_ac_notify(ctx, false);
        xremote_ac_switch_to_view(ctx, XRemoteViewAcSmart);
        return;
    }

    const uint32_t old_index = (uint32_t)ctx->delete_ac;
    char path[XREMOTE_AC_PATH_LEN];
    xremote_ac_ac_path(ctx->delete_name, path, sizeof(path));
    const FS_Error error = storage_common_remove(ctx->storage, path);
    const bool deleted = (error == FSE_OK) || (error == FSE_NOT_EXIST);

    ctx->delete_ac = -1;
    ctx->delete_name[0] = '\0';

    if(deleted) {
        ctx->current_ac = -1;
        xremote_ac_rebuild_menu(ctx);
        if(ctx->ac_count > 0) {
            const uint32_t next_index = (old_index < ctx->ac_count) ? old_index : ctx->ac_count - 1;
            submenu_set_selected_item(ctx->app->submenu, XRemoteAcMenuOpenBase + next_index);
        } else {
            submenu_set_selected_item(ctx->app->submenu, XRemoteAcMenuAddSmart);
        }
    }

    xremote_ac_notify(ctx, deleted);
    xremote_ac_switch_to_view(ctx, XRemoteViewAcSmart);
}

static void xremote_ac_delete_dialog_callback(DialogExResult result, void* context) {
    XRemoteAcContext* ctx = context;
    if(result == DialogExResultRight) {
        xremote_ac_delete_pending(ctx);
    } else if(result == DialogExResultLeft || result == DialogExResultCenter) {
        ctx->delete_ac = -1;
        ctx->delete_name[0] = '\0';
        xremote_ac_switch_to_view(ctx, XRemoteViewAcSmart);
    }
}

static void xremote_ac_submenu_callback(void* context, uint32_t index) {
    XRemoteApp* app = context;
    XRemoteAcContext* ctx = app->context;
    if(index == XRemoteAcMenuAddSmart) {
        xremote_ac_start_name_input(ctx, XRemoteAcFlowSmartOff);
    } else if(index == XRemoteAcMenuAddSimple) {
        xremote_ac_start_name_input(ctx, XRemoteAcFlowSimpleLearn);
    } else if(index >= XRemoteAcMenuOpenBase) {
        const uint32_t ac_index = index - XRemoteAcMenuOpenBase;
        xremote_ac_open_ac_index(ctx, ac_index);
    }
}

static void xremote_ac_submenu_callback_ex(void* context, InputType input_type, uint32_t index) {
    XRemoteAcContext* ctx = context;
    if(index < XRemoteAcMenuOpenBase) return;

    const uint32_t ac_index = index - XRemoteAcMenuOpenBase;
    if(input_type == InputTypeShort) {
        xremote_ac_open_ac_index(ctx, ac_index);
    } else if(input_type == InputTypeLong) {
        xremote_ac_show_delete_dialog(ctx, ac_index);
    }
}

static void xremote_ac_context_free(void* context) {
    XRemoteAcContext* ctx = context;
    xremote_ac_cancel_flow(ctx);
    xremote_ac_rx_free(ctx);

    ViewDispatcher* view_disp = ctx->app_ctx->view_dispatcher;
    view_dispatcher_set_custom_event_callback(view_disp, NULL);
    view_dispatcher_set_navigation_event_callback(view_disp, NULL);
    view_dispatcher_set_event_callback_context(view_disp, NULL);

    view_dispatcher_remove_view(view_disp, XRemoteViewAcSmartTextInput);
    text_input_free(ctx->text_input);
    view_dispatcher_remove_view(view_disp, XRemoteViewAcSmartDelete);
    dialog_ex_free(ctx->delete_dialog);
    view_dispatcher_remove_view(view_disp, XRemoteViewAcSmartRemote);
    ac_remote_panel_free(ctx->panel);
    view_dispatcher_remove_view(view_disp, XRemoteViewAcSmartLearn);
    learn_view_free(ctx->learn_view);
    view_dispatcher_remove_view(view_disp, XRemoteViewAcSmartSweep);
    sweep_view_free(ctx->sweep_view);

    ac_remote_reset(&ctx->remote);
    ac_remote_reset(&ctx->staged);
    ac_ir_signal_reset(&ctx->capture);
    ac_ir_signal_reset(&ctx->off_capture);
    for(size_t i = 0; i < AC_SWEEP_MAX; i++) ac_ir_signal_reset(&ctx->sweep[i]);
    furi_mutex_free(ctx->signal_mutex);
    furi_string_free(ctx->str);
    furi_record_close(RECORD_STORAGE);
    free(ctx);
}

static XRemoteAcContext* xremote_ac_context_alloc(XRemoteApp* app) {
    XRemoteAcContext* ctx = malloc(sizeof(XRemoteAcContext));
    memset(ctx, 0, sizeof(*ctx));
    ctx->app = app;
    ctx->app_ctx = app->app_ctx;
    ctx->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(ctx->storage, XREMOTE_AC_DIR);

    ctx->text_input = text_input_alloc();
    text_input_set_validator(ctx->text_input, NULL, ctx);
    View* text_view = text_input_get_view(ctx->text_input);
    view_set_previous_callback(text_view, xremote_ac_text_input_exit_callback);
    view_dispatcher_add_view(ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartTextInput, text_view);

    ctx->delete_dialog = dialog_ex_alloc();
    View* delete_view = dialog_ex_get_view(ctx->delete_dialog);
    view_set_previous_callback(delete_view, xremote_ac_delete_dialog_exit_callback);
    view_dispatcher_add_view(ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartDelete, delete_view);

    ctx->panel = ac_remote_panel_alloc();
    view_dispatcher_add_view(
        ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartRemote, ac_remote_panel_get_view(ctx->panel));

    ctx->learn_view = learn_view_alloc();
    learn_view_set_callback(ctx->learn_view, xremote_ac_learn_view_callback, ctx);
    view_dispatcher_add_view(
        ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartLearn, learn_view_get_view(ctx->learn_view));

    ctx->sweep_view = sweep_view_alloc();
    sweep_view_set_callback(ctx->sweep_view, xremote_ac_sweep_view_callback, ctx);
    view_dispatcher_add_view(
        ctx->app_ctx->view_dispatcher, XRemoteViewAcSmartSweep, sweep_view_get_view(ctx->sweep_view));

    ctx->signal_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    ctx->str = furi_string_alloc();
    ctx->current_ac = -1;
    ctx->delete_ac = -1;
    ctx->current_view = XRemoteViewAcSmart;
    ctx->temp_display = 20;
    ctx->sweep_temp_start = 16;

    view_dispatcher_set_custom_event_callback(
        ctx->app_ctx->view_dispatcher, xremote_ac_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        ctx->app_ctx->view_dispatcher, xremote_ac_back_event_callback);
    view_dispatcher_set_event_callback_context(ctx->app_ctx->view_dispatcher, ctx);
    return ctx;
}

XRemoteApp* xremote_ac_alloc(XRemoteAppContext* app_ctx) {
    XRemoteApp* app = xremote_app_alloc(app_ctx);
    xremote_app_submenu_alloc(app, XRemoteViewAcSmart, xremote_ac_submenu_exit_callback);

    XRemoteAcContext* ctx = xremote_ac_context_alloc(app);
    xremote_app_set_user_context(app, ctx, xremote_ac_context_free);
    xremote_ac_rebuild_menu(ctx);
    return app;
}
