/*!
 *  @file flipper-xremote/views/xremote_universal_view.c
 *
 * @brief Universal Remote style typed layouts for saved XRemote remotes.
 */

#include "xremote_universal_view.h"
#include "../xremote_app.h"

#include <gui/modules/button_panel.h>

extern const Icon I_blue_19x20;
extern const Icon I_blue_hover_19x20;
extern const Icon I_brightness_text_40x5;
extern const Icon I_celsius_24x23;
extern const Icon I_celsius_hover_24x23;
extern const Icon I_ch_down_24x21;
extern const Icon I_ch_down_hover_24x21;
extern const Icon I_ch_text_31x34;
extern const Icon I_ch_up_24x21;
extern const Icon I_ch_up_hover_24x21;
extern const Icon I_color_text_24x5;
extern const Icon I_cool_30x51;
extern const Icon I_dry_19x20;
extern const Icon I_dry_hover_19x20;
extern const Icon I_dry_text_15x5;
extern const Icon I_fahren_24x23;
extern const Icon I_fahren_hover_24x23;
extern const Icon I_green_19x20;
extern const Icon I_green_hover_19x20;
extern const Icon I_heat_30x51;
extern const Icon I_max_24x23;
extern const Icon I_max_hover_24x23;
extern const Icon I_minus_19x20;
extern const Icon I_minus_hover_19x20;
extern const Icon I_mode_19x20;
extern const Icon I_mode_hover_19x20;
extern const Icon I_mode_text_20x5;
extern const Icon I_mute_19x20;
extern const Icon I_mute_hover_19x20;
extern const Icon I_mute_text_19x5;
extern const Icon I_next_19x20;
extern const Icon I_next_hover_19x20;
extern const Icon I_next_text_19x6;
extern const Icon I_off_19x20;
extern const Icon I_off_hover_19x20;
extern const Icon I_off_text_12x5;
extern const Icon I_on_text_9x5;
extern const Icon I_pause_19x20;
extern const Icon I_pause_hover_19x20;
extern const Icon I_pause_text_23x5;
extern const Icon I_play_19x20;
extern const Icon I_play_hover_19x20;
extern const Icon I_play_text_19x5;
extern const Icon I_plus_19x20;
extern const Icon I_plus_hover_19x20;
extern const Icon I_power_19x20;
extern const Icon I_power_hover_19x20;
extern const Icon I_power_text_24x5;
extern const Icon I_prev_19x20;
extern const Icon I_prev_hover_19x20;
extern const Icon I_prev_text_19x5;
extern const Icon I_red_19x20;
extern const Icon I_red_hover_19x20;
extern const Icon I_rotate_19x20;
extern const Icon I_rotate_hover_19x20;
extern const Icon I_rotate_text_24x5;
extern const Icon I_speed_text_30x30;
extern const Icon I_timer_19x20;
extern const Icon I_timer_hover_19x20;
extern const Icon I_timer_text_23x5;
extern const Icon I_vol_ac_text_30x30;
extern const Icon I_vol_tv_text_29x34;
extern const Icon I_voldown_24x21;
extern const Icon I_voldown_hover_24x21;
extern const Icon I_volup_24x21;
extern const Icon I_volup_hover_24x21;
extern const Icon I_white_19x20;
extern const Icon I_white_hover_19x20;

typedef struct {
    ButtonPanel* button_panel;
    XRemoteAppButtons* buttons;
    uint8_t selected;
    uint8_t page;
    bool pressed;
} XRemoteUniversalContext;

#define XREMOTE_AC_PAGE_MAIN    0
#define XREMOTE_AC_PAGE_COMFORT 1
#define XREMOTE_AC_PAGE_TIMER   2
#define XREMOTE_AC_INDEX_NEXT 1000
#define XREMOTE_AC_INDEX_PREV 1001

static void xremote_hisense_ac_build_page(XRemoteUniversalContext* universal);

static void xremote_universal_item_callback(void* context, uint32_t index, InputType type) {
    XRemoteUniversalContext* universal = context;
    xremote_app_assert_void(universal);
    xremote_app_assert_void(universal->buttons);

    if(type != InputTypeShort) return;

    const char* button_name = xremote_button_get_name(index);
    xremote_app_assert_void(button_name);

    InfraredRemoteButton* button =
        infrared_remote_get_button_by_name(universal->buttons->remote, button_name);

    if(button) {
        InfraredSignal* signal = infrared_remote_button_get_signal(button);
        xremote_app_send_signal(universal->buttons->app_ctx, signal);
        dolphin_deed(DolphinDeedIrSend);
    }
}

static void xremote_hisense_ac_callback(void* context, uint32_t index, InputType type) {
    XRemoteUniversalContext* universal = context;
    xremote_app_assert_void(universal);
    xremote_app_assert_void(universal->buttons);

    if(type != InputTypeShort) return;

    if(index == XREMOTE_AC_INDEX_NEXT) {
        if(universal->page < XREMOTE_AC_PAGE_TIMER) {
            universal->page++;
        }
        xremote_hisense_ac_build_page(universal);
        return;
    }

    if(index == XREMOTE_AC_INDEX_PREV) {
        if(universal->page > XREMOTE_AC_PAGE_MAIN) {
            universal->page--;
        }
        xremote_hisense_ac_build_page(universal);
        return;
    }

    const char* button_name = xremote_button_get_name(index);
    xremote_app_assert_void(button_name);

    InfraredRemoteButton* button =
        infrared_remote_get_button_by_name(universal->buttons->remote, button_name);

    if(button) {
        InfraredSignal* signal = infrared_remote_button_get_signal(button);
        xremote_app_send_signal(universal->buttons->app_ctx, signal);
        dolphin_deed(DolphinDeedIrSend);
    }
}

static void xremote_hisense_ac_add_button(
    XRemoteUniversalContext* universal,
    const char* command_name,
    uint8_t matrix_x,
    uint8_t matrix_y,
    uint8_t x,
    uint8_t y,
    const Icon* icon,
    const Icon* icon_selected,
    const char* label,
    uint8_t label_x,
    uint8_t label_y) {
    const int button_index = xremote_button_get_index(command_name);
    furi_check(button_index >= 0);

    button_panel_add_item(
        universal->button_panel,
        (uint32_t)button_index,
        matrix_x,
        matrix_y,
        x,
        y,
        icon,
        icon_selected,
        xremote_hisense_ac_callback,
        universal);
    if(label) {
        button_panel_add_label(universal->button_panel, label_x, label_y, FontSecondary, label);
    }
}

static void xremote_hisense_ac_add_nav(
    XRemoteUniversalContext* universal,
    uint32_t index,
    uint8_t matrix_x,
    uint8_t matrix_y,
    uint8_t x,
    uint8_t y,
    const Icon* icon,
    const Icon* icon_selected,
    const char* label,
    uint8_t label_x,
    uint8_t label_y) {
    button_panel_add_item(
        universal->button_panel,
        index,
        matrix_x,
        matrix_y,
        x,
        y,
        icon,
        icon_selected,
        xremote_hisense_ac_callback,
        universal);
    if(label) {
        button_panel_add_label(universal->button_panel, label_x, label_y, FontSecondary, label);
    }
}

static void xremote_hisense_ac_add_header(
    XRemoteUniversalContext* universal,
    const char* page_label) {
    button_panel_add_label(universal->button_panel, 25, 10, FontPrimary, "AC");
    button_panel_add_label(universal->button_panel, 50, 10, FontSecondary, page_label);
}

static void xremote_hisense_ac_build_main_page(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 4);
    xremote_hisense_ac_add_header(universal, "1/3");

    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_POWER, 0, 0, 6, 13, &I_power_19x20, &I_power_hover_19x20, "Pwr", 6, 41);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_MODE, 1, 0, 39, 13, &I_mode_19x20, &I_mode_hover_19x20, "Mode", 38, 41);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_SMART, 0, 1, 6, 42, &I_mode_19x20, &I_mode_hover_19x20, "Smt", 6, 70);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_FAN_SPEED, 1, 1, 39, 42, &I_rotate_19x20, &I_rotate_hover_19x20, "Fan", 41, 70);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_TEMP_UP, 0, 2, 6, 71, &I_plus_19x20, &I_plus_hover_19x20, "T+", 9, 99);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_TEMP_DOWN, 1, 2, 39, 71, &I_minus_19x20, &I_minus_hover_19x20, "T-", 42, 99);
    xremote_hisense_ac_add_nav(
        universal, XREMOTE_AC_INDEX_NEXT, 1, 3, 39, 101, &I_next_19x20, &I_next_hover_19x20, NULL, 0, 0);
}

static void xremote_hisense_ac_build_comfort_page(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 4);
    xremote_hisense_ac_add_header(universal, "2/3");

    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_SUPER, 0, 0, 3, 15, &I_max_24x23, &I_max_hover_24x23, NULL, 0, 0);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_SLEEP, 1, 0, 39, 17, &I_pause_19x20, &I_pause_hover_19x20, NULL, 0, 0);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_I_FEEL, 0, 1, 3, 50, &I_celsius_24x23, &I_celsius_hover_24x23, NULL, 0, 0);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_QUIET, 1, 1, 39, 52, &I_mute_19x20, &I_mute_hover_19x20, NULL, 0, 0);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_DIMMER, 0, 2, 6, 86, &I_minus_19x20, &I_minus_hover_19x20, NULL, 0, 0);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_ECONOMY, 1, 2, 39, 86, &I_green_19x20, &I_green_hover_19x20, NULL, 0, 0);
    xremote_hisense_ac_add_nav(
        universal, XREMOTE_AC_INDEX_PREV, 0, 3, 6, 109, &I_prev_19x20, &I_prev_hover_19x20, NULL, 0, 0);
    xremote_hisense_ac_add_nav(
        universal, XREMOTE_AC_INDEX_NEXT, 1, 3, 39, 109, &I_next_19x20, &I_next_hover_19x20, NULL, 0, 0);
}

static void xremote_hisense_ac_build_timer_page(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 4);
    xremote_hisense_ac_add_header(universal, "3/3");

    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_SWING_V, 0, 0, 6, 13, &I_rotate_19x20, &I_rotate_hover_19x20, "V", 12, 41);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_SWING_H, 1, 0, 39, 13, &I_rotate_19x20, &I_rotate_hover_19x20, "H", 45, 41);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_CLOCK, 0, 1, 6, 42, &I_timer_19x20, &I_timer_hover_19x20, "Clk", 6, 70);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_TIMER_ON, 1, 1, 39, 42, &I_timer_19x20, &I_timer_hover_19x20, "On", 41, 70);
    xremote_hisense_ac_add_button(
        universal, XREMOTE_COMMAND_TIMER_OFF, 0, 2, 6, 78, &I_timer_19x20, &I_timer_hover_19x20, "Off", 5, 106);
    xremote_hisense_ac_add_nav(
        universal, XREMOTE_AC_INDEX_PREV, 0, 3, 6, 109, &I_prev_19x20, &I_prev_hover_19x20, NULL, 0, 0);
}

static void xremote_hisense_ac_build_page(XRemoteUniversalContext* universal) {
    button_panel_reset(universal->button_panel);
    button_panel_reset_selection(universal->button_panel);

    switch(universal->page) {
    case XREMOTE_AC_PAGE_MAIN:
        xremote_hisense_ac_build_main_page(universal);
        break;
    case XREMOTE_AC_PAGE_COMFORT:
        xremote_hisense_ac_build_comfort_page(universal);
        break;
    case XREMOTE_AC_PAGE_TIMER:
        xremote_hisense_ac_build_timer_page(universal);
        break;
    default:
        universal->page = XREMOTE_AC_PAGE_MAIN;
        xremote_hisense_ac_build_main_page(universal);
        break;
    }
}

static void xremote_universal_add_item(
    XRemoteUniversalContext* universal,
    const char* command_name,
    uint16_t matrix_place_x,
    uint16_t matrix_place_y,
    uint16_t x,
    uint16_t y,
    const Icon* icon,
    const Icon* icon_selected) {
    const int button_index = xremote_button_get_index(command_name);
    furi_check(button_index >= 0);

    button_panel_add_item(
        universal->button_panel,
        (uint32_t)button_index,
        matrix_place_x,
        matrix_place_y,
        x,
        y,
        icon,
        icon_selected,
        xremote_universal_item_callback,
        universal);
}

static void xremote_universal_build_tv(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 3);

    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_POWER, 0, 0, 6, 16, &I_power_19x20, &I_power_hover_19x20);
    button_panel_add_icon(button_panel, 4, 38, &I_power_text_24x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_MUTE, 1, 0, 39, 16, &I_mute_19x20, &I_mute_hover_19x20);
    button_panel_add_icon(button_panel, 39, 38, &I_mute_text_19x5);

    button_panel_add_icon(button_panel, 0, 66, &I_ch_text_31x34);
    button_panel_add_icon(button_panel, 35, 66, &I_vol_tv_text_29x34);

    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_VOL_UP, 1, 1, 38, 53, &I_volup_24x21, &I_volup_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_NEXT_CHAN, 0, 1, 3, 53, &I_ch_up_24x21, &I_ch_up_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_VOL_DOWN, 1, 2, 38, 91, &I_voldown_24x21, &I_voldown_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_PREV_CHAN, 0, 2, 3, 91, &I_ch_down_24x21, &I_ch_down_hover_24x21);

    button_panel_add_label(button_panel, 25, 10, FontPrimary, "TV");
}

static void xremote_universal_build_audio(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 4);

    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_POWER, 0, 0, 6, 13, &I_power_19x20, &I_power_hover_19x20);
    button_panel_add_icon(button_panel, 4, 35, &I_power_text_24x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_MUTE, 1, 0, 39, 13, &I_mute_19x20, &I_mute_hover_19x20);
    button_panel_add_icon(button_panel, 39, 35, &I_mute_text_19x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_PLAY, 0, 1, 6, 42, &I_play_19x20, &I_play_hover_19x20);
    button_panel_add_icon(button_panel, 6, 64, &I_play_text_19x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_PAUSE, 0, 2, 6, 71, &I_pause_19x20, &I_pause_hover_19x20);
    button_panel_add_icon(button_panel, 4, 93, &I_pause_text_23x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_JUMP_BACKWARD, 0, 3, 6, 101, &I_prev_19x20, &I_prev_hover_19x20);
    button_panel_add_icon(button_panel, 6, 123, &I_prev_text_19x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_JUMP_FORWARD, 1, 3, 39, 101, &I_next_19x20, &I_next_hover_19x20);
    button_panel_add_icon(button_panel, 39, 123, &I_next_text_19x6);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_VOL_DOWN, 1, 2, 37, 77, &I_voldown_24x21, &I_voldown_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_VOL_UP, 1, 1, 37, 43, &I_volup_24x21, &I_volup_hover_24x21);

    button_panel_add_label(button_panel, 1, 10, FontPrimary, "Audio player");
    button_panel_add_icon(button_panel, 34, 56, &I_vol_ac_text_30x30);
}

static void xremote_universal_build_projector(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 3);

    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_POWER, 0, 0, 6, 24, &I_power_19x20, &I_power_hover_19x20);
    button_panel_add_icon(button_panel, 4, 46, &I_power_text_24x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_MUTE, 1, 0, 39, 24, &I_mute_19x20, &I_mute_hover_19x20);
    button_panel_add_icon(button_panel, 39, 46, &I_mute_text_19x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_VOL_UP, 1, 1, 37, 55, &I_volup_24x21, &I_volup_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_VOL_DOWN, 1, 2, 37, 89, &I_voldown_24x21, &I_voldown_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_PLAY, 0, 1, 6, 58, &I_play_19x20, &I_play_hover_19x20);
    button_panel_add_icon(button_panel, 6, 80, &I_play_text_19x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_PAUSE, 0, 2, 6, 87, &I_pause_19x20, &I_pause_hover_19x20);
    button_panel_add_icon(button_panel, 4, 109, &I_pause_text_23x5);

    button_panel_add_label(button_panel, 10, 11, FontPrimary, "Projector");
    button_panel_add_icon(button_panel, 34, 68, &I_vol_ac_text_30x30);
}

static void xremote_universal_build_leds(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 4);

    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_POWER_ON, 0, 0, 10, 12, &I_power_19x20, &I_power_hover_19x20);
    button_panel_add_icon(button_panel, 15, 34, &I_on_text_9x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_POWER_OFF, 1, 0, 35, 12, &I_off_19x20, &I_off_hover_19x20);
    button_panel_add_icon(button_panel, 38, 34, &I_off_text_12x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_BRIGHTNESS_UP, 0, 1, 10, 42, &I_plus_19x20, &I_plus_hover_19x20);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_BRIGHTNESS_DN, 1, 1, 35, 42, &I_minus_19x20, &I_minus_hover_19x20);
    button_panel_add_icon(button_panel, 12, 64, &I_brightness_text_40x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_RED, 0, 2, 10, 74, &I_red_19x20, &I_red_hover_19x20);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_GREEN, 1, 2, 35, 74, &I_green_19x20, &I_green_hover_19x20);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_BLUE, 0, 3, 10, 99, &I_blue_19x20, &I_blue_hover_19x20);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_WHITE, 1, 3, 35, 99, &I_white_19x20, &I_white_hover_19x20);
    button_panel_add_icon(button_panel, 19, 121, &I_color_text_24x5);

    button_panel_add_label(button_panel, 20, 9, FontPrimary, "LEDs");
}

static void xremote_universal_build_fan(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 3);

    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_POWER, 0, 0, 6, 24, &I_power_19x20, &I_power_hover_19x20);
    button_panel_add_icon(button_panel, 4, 46, &I_power_text_24x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_MODE, 1, 0, 39, 24, &I_mode_19x20, &I_mode_hover_19x20);
    button_panel_add_icon(button_panel, 39, 46, &I_mode_text_20x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_SPEED_UP, 1, 1, 37, 55, &I_volup_24x21, &I_volup_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_SPEED_DN, 1, 2, 37, 89, &I_voldown_24x21, &I_voldown_hover_24x21);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_ROTATE, 0, 1, 6, 58, &I_rotate_19x20, &I_rotate_hover_19x20);
    button_panel_add_icon(button_panel, 4, 80, &I_rotate_text_24x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_TIMER, 0, 2, 6, 87, &I_timer_19x20, &I_timer_hover_19x20);
    button_panel_add_icon(button_panel, 4, 109, &I_timer_text_23x5);

    button_panel_add_label(button_panel, 5, 11, FontPrimary, "Fan remote");
    button_panel_add_icon(button_panel, 34, 68, &I_speed_text_30x30);
}

static void xremote_universal_build_ac(XRemoteUniversalContext* universal) {
    ButtonPanel* button_panel = universal->button_panel;
    button_panel_reserve(button_panel, 2, 3);

    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_OFF, 0, 0, 6, 15, &I_off_19x20, &I_off_hover_19x20);
    button_panel_add_icon(button_panel, 10, 37, &I_off_text_12x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_DRY, 1, 0, 39, 15, &I_dry_19x20, &I_dry_hover_19x20);
    button_panel_add_icon(button_panel, 41, 37, &I_dry_text_15x5);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_COOL_HI, 0, 1, 3, 49, &I_max_24x23, &I_max_hover_24x23);
    xremote_universal_add_item(
        universal, XREMOTE_COMMAND_HEAT_HI, 1, 1, 37, 49, &I_max_24x23, &I_max_hover_24x23);

    if(furi_hal_rtc_get_locale_units() == FuriHalRtcLocaleUnitsMetric) {
        xremote_universal_add_item(
            universal,
            XREMOTE_COMMAND_COOL_LO,
            0,
            2,
            3,
            100,
            &I_celsius_24x23,
            &I_celsius_hover_24x23);
        xremote_universal_add_item(
            universal,
            XREMOTE_COMMAND_HEAT_LO,
            1,
            2,
            37,
            100,
            &I_celsius_24x23,
            &I_celsius_hover_24x23);
    } else {
        xremote_universal_add_item(
            universal,
            XREMOTE_COMMAND_COOL_LO,
            0,
            2,
            3,
            100,
            &I_fahren_24x23,
            &I_fahren_hover_24x23);
        xremote_universal_add_item(
            universal,
            XREMOTE_COMMAND_HEAT_LO,
            1,
            2,
            37,
            100,
            &I_fahren_24x23,
            &I_fahren_hover_24x23);
    }

    button_panel_add_icon(button_panel, 0, 60, &I_cool_30x51);
    button_panel_add_icon(button_panel, 34, 60, &I_heat_30x51);
    button_panel_add_label(button_panel, 24, 10, FontPrimary, "AC");
}

static void xremote_universal_build_layout(XRemoteUniversalContext* universal) {
    switch(universal->buttons->remote_type) {
    case XRemoteRemoteTypeTV:
        xremote_universal_build_tv(universal);
        break;
    case XRemoteRemoteTypeAudio:
        xremote_universal_build_audio(universal);
        break;
    case XRemoteRemoteTypeProjector:
        xremote_universal_build_projector(universal);
        break;
    case XRemoteRemoteTypeLEDs:
        xremote_universal_build_leds(universal);
        break;
    case XRemoteRemoteTypeFan:
        xremote_universal_build_fan(universal);
        break;
    case XRemoteRemoteTypeAC:
        xremote_universal_build_ac(universal);
        break;
    case XRemoteRemoteTypeGeneric:
    default:
        break;
    }
}

static void xremote_universal_context_free(void* context) {
    XRemoteUniversalContext* universal = context;
    xremote_app_assert_void(universal);
    if(universal->button_panel) button_panel_free(universal->button_panel);
    free(universal);
}

static XRemoteView* xremote_hisense_ac_view_alloc(XRemoteAppButtons* buttons) {
    XRemoteUniversalContext* universal = malloc(sizeof(XRemoteUniversalContext));
    universal->button_panel = button_panel_alloc();
    universal->buttons = buttons;
    universal->selected = 0;
    universal->page = XREMOTE_AC_PAGE_MAIN;
    universal->pressed = false;
    xremote_hisense_ac_build_page(universal);

    XRemoteView* remote_view = xremote_view_alloc_empty();
    xremote_view_set_app_context(remote_view, buttons->app_ctx);
    xremote_view_set_view(remote_view, button_panel_get_view(universal->button_panel));
    xremote_view_set_context(remote_view, universal, xremote_universal_context_free);

    return remote_view;
}

XRemoteView* xremote_universal_view_alloc(void* app_ctx, void* model_ctx) {
    UNUSED(app_ctx);
    XRemoteAppButtons* buttons = model_ctx;
    xremote_app_assert(buttons, NULL);

    if(buttons->remote_type == XRemoteRemoteTypeAC) {
        return xremote_hisense_ac_view_alloc(buttons);
    }

    XRemoteUniversalContext* universal = malloc(sizeof(XRemoteUniversalContext));
    universal->button_panel = button_panel_alloc();
    universal->buttons = buttons;
    universal->selected = 0;
    universal->page = 0;
    universal->pressed = false;
    xremote_universal_build_layout(universal);

    XRemoteView* remote_view = xremote_view_alloc_empty();
    xremote_view_set_app_context(remote_view, buttons->app_ctx);
    xremote_view_set_view(remote_view, button_panel_get_view(universal->button_panel));
    xremote_view_set_context(remote_view, universal, xremote_universal_context_free);

    return remote_view;
}
