#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/elements.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <notification/notification_app.h>

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include "clock_app.h"

/*
    Nightstand clock with a stopwatch, display brightness control, and native
    daily RTC alarm setup.
*/

// How long the brightness bar stays on screen after Up/Down.
#define BRIGHTNESS_OSD_MS 3000

static const NotificationMessage message_red_dim = {
    .type = NotificationMessageTypeLedRed,
    .data.led.value = 0xFF / 16,
};

static const NotificationMessage message_red_off = {
    .type = NotificationMessageTypeLedRed,
    .data.led.value = 0x00,
};

static const NotificationSequence led_on = {
    &message_red_dim,
    &message_do_not_reset,
    NULL,
};

static const NotificationSequence led_off = {
    &message_red_off,
    &message_do_not_reset,
    NULL,
};

static const NotificationSequence led_reset = {
    &message_red_off,
    NULL,
};

// Use force_on (not backlight_on) so the level is pushed to the panel even when
// the backlight is already on - a plain backlight_on early-returns in that case.
// Used on exit to restore the saved brightness after the enforce lock is freed.
static void set_backlight_brightness(AppState* app, float brightness) {
    app->notification->settings.display_brightness = brightness;
    notification_message(app->notification, &sequence_display_backlight_force_on);
}

// While the app holds the backlight on with enforce_on, a plain backlight_on
// does not change the screen: the notification service early-returns when the
// backlight is already on, and the enforced "internal" layer that is actually
// shown is only set once at startup. force_on bypasses that early-return to
// update the panel now; the enforce pair then refreshes the locked internal
// layer so the new level cannot be reverted.
static void set_enforced_brightness(AppState* app, float brightness) {
    app->notification->settings.display_brightness = brightness;
    notification_message(app->notification, &sequence_display_backlight_force_on);
    notification_message(app->notification, &sequence_display_backlight_enforce_auto);
    notification_message(app->notification, &sequence_display_backlight_enforce_on);
}

static void show_brightness_osd(AppState* app) {
    app->brightness_shown_until = furi_get_tick() + furi_ms_to_ticks(BRIGHTNESS_OSD_MS);
}

static void handle_up(AppState* app) {
    show_brightness_osd(app);
    if(app->brightness < 100) {
        app->nightlight_enabled = false;
        notification_message(app->notification, &led_off);
        app->brightness += 5;
        if(app->brightness > 100) app->brightness = 100;
    }
    set_enforced_brightness(app, (float)(app->brightness / 100.f));
}

static void handle_down(AppState* app) {
    show_brightness_osd(app);
    if(app->brightness > 0) {
        app->brightness -= 5;
        if(app->brightness == 0) {
            app->nightlight_enabled = true;
            notification_message(app->notification, &led_on);
        }
    } else {
        app->nightlight_enabled = !app->nightlight_enabled;
        if(app->nightlight_enabled) {
            notification_message(app->notification, &led_on);
        } else {
            notification_message(app->notification, &led_off);
        }
    }
    set_enforced_brightness(app, (float)(app->brightness / 100.f));
}

static void
    clock_draw_brightness_bar(Canvas* canvas, uint8_t x, uint8_t y, uint8_t height, float progress) {
    furi_assert(canvas);
    furi_assert(((float)progress >= 0.0f) && ((float)progress <= 1.0f));

    uint8_t width = 9;

    uint8_t progress_length = roundf((1.f - progress) * (height - 2));

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, x + 1, y + 1, width - 2, height - 2);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x + 1, y + 1, width - 2, progress_length);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, x, y, width, height, 3);
}

// The alarm is stored as 24h internally; these convert for locale-aware display
// and editing (12h + AM/PM when the system time format is 12h).
static void to_12h(uint8_t h24, uint8_t* h12, bool* pm) {
    *pm = h24 >= 12;
    uint8_t h = h24 % 12;
    *h12 = (h == 0) ? 12 : h;
}

static uint8_t to_24h(uint8_t h12, bool pm) {
    return (h12 % 12) + (pm ? 12 : 0); // 12 -> 0, so 12AM=00:00, 12PM=12:00
}

static void
    format_alarm_time(char* buf, size_t n, uint8_t h24, uint8_t minute, LocaleTimeFormat fmt) {
    if(fmt == LocaleTimeFormat12h) {
        uint8_t h12;
        bool pm;
        to_12h(h24, &h12, &pm);
        snprintf(buf, n, "%u:%.2u %s", h12, minute, pm ? "PM" : "AM");
    } else {
        snprintf(buf, n, "%.2u:%.2u", h24, minute);
    }
}

static void clock_render_callback(Canvas* const canvas, void* ctx) {
    ClockModel* model = ctx;
    AppState* state = model->app;

    if((int32_t)(state->brightness_shown_until - furi_get_tick()) > 0) {
        clock_draw_brightness_bar(canvas, 119, 1, 62, (float)(state->brightness / 100.f));
    }

    DateTime curr_dt;
    furi_hal_rtc_get_datetime(&curr_dt);
    uint32_t curr_ts = datetime_datetime_to_timestamp(&curr_dt);

    char time_string[TIME_LEN];
    char date_string[DATE_LEN];
    char meridian_string[MERIDIAN_LEN];
    char date_pct_string[DATE_PCT_LEN];
    char timer_string[20];

    if(state->time_format == LocaleTimeFormat24h) {
        snprintf(
            time_string, TIME_LEN, CLOCK_TIME_FORMAT, curr_dt.hour, curr_dt.minute, curr_dt.second);
    } else {
        bool pm12 = curr_dt.hour >= 12;
        uint8_t hour12 = curr_dt.hour % 12;
        if(hour12 == 0) hour12 = 12;
        snprintf(time_string, TIME_LEN, CLOCK_TIME_FORMAT, hour12, curr_dt.minute, curr_dt.second);

        snprintf(
            meridian_string,
            MERIDIAN_LEN,
            MERIDIAN_FORMAT,
            pm12 ? MERIDIAN_STRING_PM : MERIDIAN_STRING_AM);
    }

    if(state->date_format == LocaleDateFormatYMD) {
        snprintf(
            date_string, DATE_LEN, CLOCK_ISO_DATE_FORMAT, curr_dt.year, curr_dt.month, curr_dt.day);
    } else if(state->date_format == LocaleDateFormatMDY) {
        snprintf(
            date_string, DATE_LEN, CLOCK_RFC_DATE_FORMAT, curr_dt.month, curr_dt.day, curr_dt.year);
    } else {
        snprintf(
            date_string, DATE_LEN, CLOCK_RFC_DATE_FORMAT, curr_dt.day, curr_dt.month, curr_dt.year);
    }

    bool timer_running = state->timer_running;
    uint32_t timer_start_timestamp = state->timer_start_timestamp;
    uint32_t timer_stopped_seconds = state->timer_stopped_seconds;

    canvas_set_font(canvas, FontBigNumbers);

    if(timer_start_timestamp != 0) {
        int32_t elapsed_secs = timer_running ? (curr_ts - timer_start_timestamp) :
                                               timer_stopped_seconds;
        snprintf(timer_string, 20, "%.2ld:%.2ld", elapsed_secs / 60, elapsed_secs % 60);
        if(state->time_format == LocaleTimeFormat12h) {
            canvas_draw_str_aligned(
                canvas, 56, 8, AlignCenter, AlignCenter, time_string); // DRAW TIME
        } else {
            canvas_draw_str_aligned(
                canvas, 64, 8, AlignCenter, AlignCenter, time_string); // DRAW TIME
        }
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, timer_string); // DRAW TIMER
        canvas_set_font(canvas, FontSecondary);
        if(state->time_format == LocaleTimeFormat12h) {
            canvas_draw_str_aligned(canvas, 112, 8, AlignCenter, AlignCenter, meridian_string);
        }

        snprintf(
            date_pct_string, sizeof(date_pct_string), "%s   %u%%", date_string, state->battery_pct);
        canvas_draw_str_aligned(
            canvas, 64, 20, AlignCenter, AlignTop, date_pct_string); // DRAW DATE + BATTERY
        elements_button_left(canvas, "Reset");
    } else {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, time_string);
        canvas_set_font(canvas, FontSecondary);

        if(state->time_format == LocaleTimeFormat12h) {
            snprintf(
                date_pct_string,
                sizeof(date_pct_string),
                "%s   %u%%",
                date_string,
                state->battery_pct);
            canvas_draw_str_aligned(canvas, 64, 17, AlignCenter, AlignCenter, date_pct_string);
            canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignCenter, meridian_string);
        } else {
            canvas_draw_str_aligned(canvas, 64, 17, AlignCenter, AlignCenter, date_string);
            snprintf(date_pct_string, sizeof(date_pct_string), "%u%%", state->battery_pct);
            canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignCenter, date_pct_string);
        }
    }
    if(timer_running) {
        elements_button_center(canvas, "Stop");
    } else if(timer_start_timestamp != 0 && !timer_running) {
        elements_button_center(canvas, "Start");
    }
    elements_button_right(canvas, "Alarm");

    // Show the native RTC alarm time when armed. It stays out of the stopwatch
    // layout to avoid colliding with the Reset button.
    if(state->alarm_enabled && timer_start_timestamp == 0) {
        char alarm_time[12];
        format_alarm_time(
            alarm_time,
            sizeof(alarm_time),
            state->alarm_time.hour,
            state->alarm_time.minute,
            state->time_format);
        char alarm_str[16];
        // '!' stands in for a bell - the default font has no bell glyph.
        snprintf(alarm_str, sizeof(alarm_str), "! %s", alarm_time);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 2, 62, AlignLeft, AlignBottom, alarm_str);
    }
}

static void timer_start_stop(AppState* plugin_state) {
    uint32_t curr_ts = furi_hal_rtc_get_timestamp();
    if(plugin_state->timer_running) {
        plugin_state->timer_stopped_seconds = curr_ts - plugin_state->timer_start_timestamp;
    } else {
        if(plugin_state->timer_start_timestamp == 0) {
            plugin_state->timer_start_timestamp = curr_ts;
        } else {
            plugin_state->timer_start_timestamp = curr_ts - plugin_state->timer_stopped_seconds;
        }
    }
    plugin_state->timer_running = !plugin_state->timer_running;
}

static void timer_reset_seconds(AppState* plugin_state) {
    if(plugin_state->timer_start_timestamp != 0) {
        plugin_state->timer_running = false;
        plugin_state->timer_start_timestamp = 0;
        plugin_state->timer_stopped_seconds = 0;
    }
}

static uint8_t ns_normalize_brightness(uint8_t value) {
    if(value > 100) value = 100;
    value = (uint8_t)(((value + 2U) / 5U) * 5U);
    return value > 100 ? 100 : value;
}

static uint8_t ns_system_brightness(float value) {
    int32_t percent = (int32_t)roundf(value * 100.f);
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    return ns_normalize_brightness((uint8_t)percent);
}

static void ns_settings_save(AppState* app) {
    app->settings.brightness = app->brightness;
    if(!saved_struct_save(
           NS_SETTINGS_PATH,
           &app->settings,
           sizeof(NsSettings),
           NS_SETTINGS_MAGIC,
           NS_SETTINGS_VERSION)) {
        FURI_LOG_W(TAG, "Failed to save Clock settings");
    }
}

static void ns_settings_load(AppState* app) {
    if(saved_struct_load(
           NS_SETTINGS_PATH,
           &app->settings,
           sizeof(NsSettings),
           NS_SETTINGS_MAGIC,
           NS_SETTINGS_VERSION)) {
        app->brightness = ns_normalize_brightness(app->settings.brightness);
        return;
    }

    NsSettingsLegacyV2 legacy = {0};
    if(saved_struct_load(
           NS_SETTINGS_PATH,
           &legacy,
           sizeof(NsSettingsLegacyV2),
           NS_SETTINGS_MAGIC,
           NS_SETTINGS_LEGACY_VERSION)) {
        app->brightness = ns_normalize_brightness(legacy.brightness);
        app->settings.brightness = app->brightness;
        ns_settings_save(app);
        return;
    }

    app->brightness = ns_system_brightness(app->saved_brightness);
    app->settings.brightness = app->brightness;
}

static void clock_alarm_refresh(AppState* app) {
    app->alarm_enabled = furi_hal_rtc_get_alarm(&app->alarm_time);
    if(app->alarm_time.hour > 23 || app->alarm_time.minute > 59) {
        memset(&app->alarm_time, 0, sizeof(DateTime));
        app->alarm_time.hour = 7;
    }
    app->alarm_time.second = 0;
}

static void clock_alarm_apply(AppState* app) {
    app->alarm_time.second = 0;
    furi_hal_rtc_set_alarm(&app->alarm_time, app->alarm_enabled);
}

static void ns_switch(AppState* app, NsViewId view) {
    app->current_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

// ---------------- clock view ----------------

static void refresh_alarm_menu(AppState* app);

static bool clock_input_callback(InputEvent* event, void* context) {
    AppState* app = context;

    if(event->type != InputTypeShort) return false;

    switch(event->key) {
    case InputKeyLeft:
        timer_reset_seconds(app);
        return true;
    case InputKeyOk:
        timer_start_stop(app);
        return true;
    case InputKeyUp:
        handle_up(app);
        return true;
    case InputKeyDown:
        handle_down(app);
        return true;
    case InputKeyRight:
        clock_alarm_refresh(app);
        refresh_alarm_menu(app);
        ns_switch(app, NsViewAlarmMenu);
        return true;
    case InputKeyBack:
        // Not consumed: the dispatcher's navigation callback exits the app.
        return false;
    default:
        return false;
    }
}

// ---------------- alarm menu (VariableItemList) ----------------

static void alarm_toggle_changed(VariableItem* item) {
    AppState* app = variable_item_get_context(item);
    app->alarm_enabled = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->alarm_enabled ? "ON" : "OFF");
    clock_alarm_apply(app);
}

static void refresh_time_menu_item(AppState* app) {
    char buf[12];
    format_alarm_time(
        buf, sizeof(buf), app->alarm_time.hour, app->alarm_time.minute, app->time_format);
    variable_item_set_current_value_text(app->alarm_time_item, buf);
}

static void refresh_alarm_menu(AppState* app) {
    variable_item_set_current_value_index(app->alarm_toggle_item, app->alarm_enabled ? 1 : 0);
    variable_item_set_current_value_text(
        app->alarm_toggle_item, app->alarm_enabled ? "ON" : "OFF");
    refresh_time_menu_item(app);
}

static void alarm_menu_enter(void* context, uint32_t index) {
    AppState* app = context;
    if(index != 1) return; // only the "Set time" row opens the picker

    app->edit_hour = app->alarm_time.hour;
    app->edit_minute = app->alarm_time.minute;
    app->edit_field = 0;
    ns_switch(app, NsViewAlarmTime);
}

// ---------------- alarm time picker (custom, time only) ----------------
// A plain HH:MM picker instead of the firmware date_time_input, which always
// shows the date too. The alarm has no date - it fires every day at this time.

static void alarm_time_draw(Canvas* canvas, void* ctx) {
    ClockModel* model = ctx;
    AppState* app = model->app;
    bool h12mode = (app->time_format == LocaleTimeFormat12h);
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "Alarm time");

    char hh[4], mm[4];
    bool pm = false;
    if(h12mode) {
        uint8_t h12;
        to_12h(app->edit_hour, &h12, &pm);
        snprintf(hh, sizeof(hh), "%.2u", h12);
    } else {
        snprintf(hh, sizeof(hh), "%.2u", app->edit_hour);
    }
    snprintf(mm, sizeof(mm), "%.2u", app->edit_minute);

    // 24h: HH:MM centred. 12h: shift left to make room for AM/PM on the right.
    uint8_t hx = h12mode ? 34 : 45;
    uint8_t cx = h12mode ? 53 : 64;
    uint8_t mx = h12mode ? 72 : 83;

    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, hx, 32, AlignCenter, AlignCenter, hh);
    canvas_draw_str_aligned(canvas, cx, 32, AlignCenter, AlignCenter, ":");
    canvas_draw_str_aligned(canvas, mx, 32, AlignCenter, AlignCenter, mm);
    if(h12mode) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 104, 32, AlignCenter, AlignCenter, pm ? "PM" : "AM");
    }

    // Underline whichever field Up/Down currently changes.
    uint8_t ux, uw;
    if(app->edit_field == 0) {
        ux = hx;
        uw = 20;
    } else if(app->edit_field == 1) {
        ux = mx;
        uw = 20;
    } else { // meridian (12h only)
        ux = 104;
        uw = 18;
    }
    canvas_draw_line(canvas, ux - uw / 2, 46, ux + uw / 2, 46);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 50, AlignCenter, AlignBottom, "L/R field  U/D value");
    elements_button_center(canvas, "Save");
}

static bool alarm_time_input(InputEvent* event, void* context) {
    AppState* app = context;
    bool h12mode = (app->time_format == LocaleTimeFormat12h);
    uint8_t max_field = h12mode ? 2 : 1; // extra meridian field in 12h mode

    bool value_evt = (event->type == InputTypeShort || event->type == InputTypeRepeat);
    bool nav_evt = (event->type == InputTypeShort);

    switch(event->key) {
    case InputKeyUp:
    case InputKeyDown: {
        if(!value_evt) return false; // auto-repeat only for value changes
        bool up = (event->key == InputKeyUp);
        if(app->edit_field == 0) {
            if(h12mode) {
                uint8_t h12;
                bool pm;
                to_12h(app->edit_hour, &h12, &pm);
                h12 = up ? ((h12 % 12) + 1) : ((h12 == 1) ? 12 : h12 - 1);
                app->edit_hour = to_24h(h12, pm);
            } else {
                app->edit_hour = (app->edit_hour + (up ? 1 : 23)) % 24;
            }
        } else if(app->edit_field == 1) {
            app->edit_minute = (app->edit_minute + (up ? 1 : 59)) % 60;
        } else { // meridian: either key flips AM/PM
            uint8_t h12;
            bool pm;
            to_12h(app->edit_hour, &h12, &pm);
            app->edit_hour = to_24h(h12, !pm);
        }
        break;
    }
    case InputKeyLeft:
        if(!nav_evt) return false;
        if(app->edit_field > 0) app->edit_field--;
        break;
    case InputKeyRight:
        if(!nav_evt) return false;
        if(app->edit_field < max_field) app->edit_field++;
        break;
    case InputKeyOk:
        if(!nav_evt) return true; // swallow held repeats, act on the click
        app->alarm_time.hour = app->edit_hour;
        app->alarm_time.minute = app->edit_minute;
        clock_alarm_apply(app);
        refresh_time_menu_item(app);
        ns_switch(app, NsViewAlarmMenu);
        return true;
    case InputKeyBack:
        // Not consumed: the navigation callback returns to the alarm menu and
        // leaves the native RTC alarm unchanged.
        return false;
    default:
        return false;
    }
    with_view_model(app->alarm_time_view, ClockModel * m, { UNUSED(m); }, true);
    return true;
}

// ---------------- periodic tick ----------------

static void ns_tick(AppState* app) {
    app->battery_pct = furi_hal_power_get_pct();

    // Only the clock view is model-driven; the menu/time views repaint
    // themselves. Committing the (unchanged) model forces a redraw.
    if(app->current_view == NsViewClock) {
        with_view_model(app->clock_view, ClockModel * m, { UNUSED(m); }, true);
    }
}

static void ns_timer_callback(void* context) {
    AppState* app = context;
    // Runs on the timer task: just poke the dispatcher, do the work on its thread.
    view_dispatcher_send_custom_event(app->view_dispatcher, NsCustomTick);
}

static bool ns_custom_event_callback(void* context, uint32_t event) {
    AppState* app = context;
    if(event == NsCustomTick) {
        ns_tick(app);
        return true;
    }
    return false;
}

static bool ns_navigation_callback(void* context) {
    AppState* app = context;
    switch(app->current_view) {
    case NsViewAlarmMenu:
        ns_switch(app, NsViewClock);
        return true;
    case NsViewAlarmTime:
        ns_switch(app, NsViewAlarmMenu);
        return true;
    case NsViewClock:
    default:
        // Back from the clock exits: returning false stops the dispatcher.
        return false;
    }
}

static void clock_restore_notification(AppState* app) {
    if(!app->notification) return;

    app->notification->settings.display_off_delay_ms = app->saved_display_off_delay_ms;
    notification_message(app->notification, &sequence_display_backlight_enforce_auto);
    set_backlight_brightness(app, app->saved_brightness);
    notification_message(app->notification, &led_reset);
    furi_record_close(RECORD_NOTIFICATION);
    app->notification = NULL;
}

static void clock_free_gui(AppState* app) {
    if(app->view_dispatcher) {
        if(app->clock_view) {
            view_dispatcher_remove_view(app->view_dispatcher, NsViewClock);
        }
        if(app->alarm_menu) {
            view_dispatcher_remove_view(app->view_dispatcher, NsViewAlarmMenu);
        }
        if(app->alarm_time_view) {
            view_dispatcher_remove_view(app->view_dispatcher, NsViewAlarmTime);
        }
    }

    if(app->clock_view) view_free(app->clock_view);
    if(app->alarm_menu) variable_item_list_free(app->alarm_menu);
    if(app->alarm_time_view) view_free(app->alarm_time_view);
    if(app->view_dispatcher) view_dispatcher_free(app->view_dispatcher);
    if(app->gui) furi_record_close(RECORD_GUI);
}

int32_t clock_app(void* p) {
    UNUSED(p);
    AppState* app = calloc(1, sizeof(AppState));

    app->time_format = locale_get_time_format();
    app->date_format = locale_get_date_format();
    app->battery_pct = furi_hal_power_get_pct();
    app->current_view = NsViewClock;

    // Make sure this fap's data folder exists before we load/save from it.
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error mkdir_result = storage_common_mkdir(storage, NS_SETTINGS_DIR);
    furi_record_close(RECORD_STORAGE);
    if(mkdir_result != FSE_OK && mkdir_result != FSE_EXIST) {
        FURI_LOG_W(TAG, "Failed to create Clock settings directory: %d", mkdir_result);
    }

    // Save current user settings, disable backlight delay, force display always on.
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->saved_brightness = app->notification->settings.display_brightness;
    app->saved_display_off_delay_ms = app->notification->settings.display_off_delay_ms;

    ns_settings_load(app);
    clock_alarm_refresh(app);

    app->notification->settings.display_off_delay_ms = 0;
    notification_message(app->notification, &sequence_display_backlight_enforce_on);
    // Apply the restored brightness now (enforce_on above only locks in whatever
    // the system was already at).
    set_enforced_brightness(app, (float)(app->brightness / 100.f));
    notification_message(app->notification, &led_off);

    app->timer = furi_timer_alloc(ns_timer_callback, FuriTimerTypePeriodic, app);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, ns_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, ns_navigation_callback);

    // Clock view (custom)
    app->clock_view = view_alloc();
    view_allocate_model(app->clock_view, ViewModelTypeLocking, sizeof(ClockModel));
    with_view_model(app->clock_view, ClockModel * m, { m->app = app; }, false);
    view_set_context(app->clock_view, app);
    view_set_draw_callback(app->clock_view, clock_render_callback);
    view_set_input_callback(app->clock_view, clock_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, NsViewClock, app->clock_view);

    // Alarm menu
    app->alarm_menu = variable_item_list_alloc();
    app->alarm_toggle_item =
        variable_item_list_add(app->alarm_menu, "Alarm", 2, alarm_toggle_changed, app);
    variable_item_set_current_value_index(app->alarm_toggle_item, app->alarm_enabled);
    variable_item_set_current_value_text(
        app->alarm_toggle_item, app->alarm_enabled ? "ON" : "OFF");
    app->alarm_time_item = variable_item_list_add(app->alarm_menu, "Set time", 1, NULL, app);
    refresh_time_menu_item(app);
    variable_item_list_set_enter_callback(app->alarm_menu, alarm_menu_enter, app);
    view_dispatcher_add_view(
        app->view_dispatcher, NsViewAlarmMenu, variable_item_list_get_view(app->alarm_menu));

    // Alarm time picker (custom, HH:MM only)
    app->alarm_time_view = view_alloc();
    view_allocate_model(app->alarm_time_view, ViewModelTypeLocking, sizeof(ClockModel));
    with_view_model(app->alarm_time_view, ClockModel * m, { m->app = app; }, false);
    view_set_context(app->alarm_time_view, app);
    view_set_draw_callback(app->alarm_time_view, alarm_time_draw);
    view_set_input_callback(app->alarm_time_view, alarm_time_input);
    view_dispatcher_add_view(app->view_dispatcher, NsViewAlarmTime, app->alarm_time_view);

    // Periodic timer, 500ms: refresh seconds, battery, stopwatch, and the
    // temporary brightness indicator. The native alarm has its own RTC service.
    if(furi_timer_start(app->timer, furi_kernel_get_tick_frequency() / 2) != FuriStatusOk) {
        FURI_LOG_E(TAG, "Failed to start Clock refresh timer");
        clock_free_gui(app);
        furi_timer_free(app->timer);
        clock_restore_notification(app);
        free(app);
        return 1;
    }

    ns_switch(app, NsViewClock);
    view_dispatcher_run(app->view_dispatcher);

    // ---- teardown ----
    // The native alarm is persisted by RTC; only Clock's brightness is private.
    ns_settings_save(app);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    clock_free_gui(app);
    clock_restore_notification(app);

    free(app);
    return 0;
}
