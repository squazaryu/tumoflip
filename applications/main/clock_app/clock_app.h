#pragma once

#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <input/input.h>
#include <locale/locale.h>
#include <notification/notification.h>
#include <datetime/datetime.h>
#include <storage/storage.h>

#define TAG "Clock"

#define CLOCK_ISO_DATE_FORMAT "%.4d-%.2d-%.2d"
#define CLOCK_RFC_DATE_FORMAT "%.2d-%.2d-%.4d"
#define CLOCK_TIME_FORMAT     "%.2d:%.2d:%.2d"

#define MERIDIAN_FORMAT    "%s"
#define MERIDIAN_STRING_AM "AM"
#define MERIDIAN_STRING_PM "PM"

#define TIME_LEN     12
#define DATE_LEN     14
#define MERIDIAN_LEN 3
#define BATTERY_LEN  4
#define DATE_PCT_LEN 21

// The alarm itself is stored by the firmware RTC service. Clock only persists
// its private nightstand display brightness.
#define NS_SETTINGS_PATH           EXT_PATH("apps_data/clock/settings.save")
#define NS_SETTINGS_DIR            EXT_PATH("apps_data/clock")
#define NS_SETTINGS_MAGIC          0x4E // 'N'
#define NS_SETTINGS_VERSION        3
#define NS_SETTINGS_LEGACY_VERSION 2

typedef enum {
    NsViewClock,
    NsViewAlarmMenu,
    NsViewAlarmTime,
} NsViewId;

// The periodic timer runs on its own task; it only pokes the dispatcher with
// this custom event so all the state work happens on the GUI thread.
typedef enum {
    NsCustomTick = 100,
} NsCustomEvent;

typedef struct {
    uint8_t brightness; // 0..100
} NsSettings;

// Upstream Nightstand Clock 1.3 used a private, app-only alarm. Read its
// settings once so an existing user keeps the saved brightness, then migrate
// to the native RTC alarm and the compact v3 settings record.
typedef struct {
    bool alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t brightness;
} NsSettingsLegacyV2;

typedef struct {
    // GUI infra
    Gui* gui;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    View* clock_view;
    VariableItemList* alarm_menu;
    View* alarm_time_view; // custom HH:MM picker (time only, no date)
    VariableItem* alarm_toggle_item;
    VariableItem* alarm_time_item;
    FuriTimer* timer;
    NsViewId current_view;

    // Clock display
    LocaleDateFormat date_format;
    LocaleTimeFormat time_format;
    uint8_t battery_pct;

    // Stopwatch (unchanged behaviour from the original app)
    uint32_t timer_start_timestamp;
    uint32_t timer_stopped_seconds;
    bool timer_running;

    // Nightstand display state. The original notification settings are restored
    // before the notification record is closed.
    float saved_brightness;
    uint32_t saved_display_off_delay_ms;
    uint8_t brightness;
    bool nightlight_enabled;
    uint32_t brightness_shown_until;

    // Native daily RTC alarm. This remains active after Clock exits and is
    // serviced by the existing firmware alarm UI.
    DateTime alarm_time;
    bool alarm_enabled;

    // Persisted app-private settings and alarm editor scratch values.
    NsSettings settings;
    uint8_t edit_hour; // scratch values the time picker edits
    uint8_t edit_minute;
    uint8_t edit_field; // 0 = hour, 1 = minute
} AppState;

// The clock view's model just points back at the app; the draw reads through it,
// and committing it (even unchanged) is what forces a redraw from the timer tick.
typedef struct {
    AppState* app;
} ClockModel;
