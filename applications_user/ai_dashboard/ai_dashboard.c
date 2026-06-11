#include <furi.h>
#include <furi_hal_bt.h>
#include <bt/bt_service/bt.h>
#include <ble/core/auto/ble_types.h>
#include <ble/core/ble_defs.h>
#include <ble/core/ble_std.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <gap.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/variable_item_list.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <toolbox/stream/file_stream.h>
#include <toolbox/stream/stream.h>
#include <profiles/serial_profile.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai_dashboard_icons.h"

// Display strings are intentionally truncated to fit the 128x64 screen.
// (OFW ufbt didn't flag these; the stricter Unleashed build does.)
#pragma GCC diagnostic ignored "-Wformat-truncation"

#define AI_DASHBOARD_MAX_PROVIDERS  12
#define AI_DASHBOARD_MAX_ID         24
#define AI_DASHBOARD_MAX_NAME       24
#define AI_DASHBOARD_MAX_ICON       4
#define AI_DASHBOARD_MAX_WINDOW     28
#define AI_DASHBOARD_MAX_RESET      28
#define AI_DASHBOARD_MAX_DETAIL     48
#define AI_DASHBOARD_MAX_SOURCE     20
#define AI_DASHBOARD_MAX_UPDATED    24
#define AI_DASHBOARD_MAX_LABEL      20
#define AI_DASHBOARD_MAX_FIELDS     12
#define AI_DASHBOARD_MAIN_ROWS      2
#define AI_DASHBOARD_BLE_BUFFER_SIZE 3072
#define AI_DASHBOARD_CLAMP(value, low, high) \
    ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

typedef enum {
    AiDashboardViewDashboard = 0,
    AiDashboardViewSettings = 1,
} AiDashboardViewId;

typedef enum {
    AiDashboardCustomEventOpenSettings = 1,
    AiDashboardCustomEventBleSnapshotUpdated = 2,
} AiDashboardCustomEvent;

typedef struct {
    char id[AI_DASHBOARD_MAX_ID];
    char name[AI_DASHBOARD_MAX_NAME];
    char icon[AI_DASHBOARD_MAX_ICON];
    char source[AI_DASHBOARD_MAX_SOURCE];
    char short_label[16];
    char short_reset[AI_DASHBOARD_MAX_RESET];
    char weekly_reset[AI_DASHBOARD_MAX_RESET];
    uint8_t short_used;
    uint8_t weekly_used;
    bool loaded;
} AiDashboardProvider;

typedef struct {
    char updated_at[AI_DASHBOARD_MAX_UPDATED];
    size_t provider_count;
    AiDashboardProvider providers[AI_DASHBOARD_MAX_PROVIDERS];
} AiDashboardSnapshot;

typedef struct {
    char provider_id[AI_DASHBOARD_MAX_ID];
    uint8_t main_mode;
} AiDashboardProviderPreference;

typedef struct {
    bool autoscroll;
    bool never_dim;
    size_t preference_count;
    AiDashboardProviderPreference preferences[AI_DASHBOARD_MAX_PROVIDERS];
} AiDashboardSettings;

typedef struct {
    AiDashboardSnapshot snapshot;
    AiDashboardSettings settings;
    uint8_t screen_index;
    uint8_t main_scroll;
    uint8_t detail_scroll;
    uint16_t tick_count;
} AiDashboardModel;

typedef struct AiDashboardApp AiDashboardApp;

typedef struct {
    AiDashboardApp* app;
    size_t preference_index;
} AiDashboardToggleBinding;

struct AiDashboardApp {
    Gui* gui;
    Storage* storage;
    Bt* bt;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    View* dashboard_view;
    VariableItemList* settings_list;
    FuriPubSubSubscription* app_bridge_sub;
    FuriMutex* ble_mutex;
    char ble_rx_buffer[AI_DASHBOARD_BLE_BUFFER_SIZE];
    size_t ble_rx_size;
    char ble_snapshot_buffer[AI_DASHBOARD_BLE_BUFFER_SIZE];
    bool ble_snapshot_ready;
    uint32_t ble_debug_chunks;
    uint32_t ble_debug_bytes;
    uint32_t ble_debug_markers;
    uint32_t ble_debug_saves;
    uint32_t ble_debug_registers;
    char provider_labels[AI_DASHBOARD_MAX_PROVIDERS][AI_DASHBOARD_MAX_LABEL];
    AiDashboardToggleBinding toggle_bindings[AI_DASHBOARD_MAX_PROVIDERS];
};

static const char* ai_dashboard_toggle_text[] = {
    "5h",
    "Weekly",
    "Hide",
};

static const char* ai_dashboard_on_off_text[] = {
    "Off",
    "On",
};

static const char* ai_dashboard_ble_end_marker = "\n<<<END>>>\n";
#define AI_DASHBOARD_APP_ID "ai_dashboard"


static void ai_dashboard_ble_debug_write(AiDashboardApp* app, const char* stage) {
    if(!app || !app->storage) {
        return;
    }

    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, APP_DATA_PATH("ble_debug.txt"), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[160];
        int len = snprintf(
            line,
            sizeof(line),
            "stage=%s chunks=%lu bytes=%lu markers=%lu saves=%lu registers=%lu rx=%lu ready=%u\n",
            stage ? stage : "unknown",
            app->ble_debug_chunks,
            app->ble_debug_bytes,
            app->ble_debug_markers,
            app->ble_debug_saves,
            app->ble_debug_registers,
            (uint32_t)app->ble_rx_size,
            app->ble_snapshot_ready ? 1 : 0);
        if(len > 0) {
            storage_file_write(file, line, (size_t)len);
        }
        storage_file_close(file);
    }
    storage_file_free(file);
}

static void ai_dashboard_str_copy(char* destination, size_t destination_size, const char* source) {
    if(destination_size == 0) {
        return;
    }

    if(!source) {
        destination[0] = '\0';
        return;
    }

    snprintf(destination, destination_size, "%s", source);
}

static void ai_dashboard_str_truncate(
    char* destination,
    size_t destination_size,
    const char* source,
    size_t max_chars) {
    if(destination_size == 0) {
        return;
    }

    destination[0] = '\0';
    if(!source) {
        return;
    }

    size_t source_length = strlen(source);
    size_t copy_length = source_length;
    if(copy_length > max_chars) {
        copy_length = max_chars;
    }
    if(copy_length > destination_size - 1) {
        copy_length = destination_size - 1;
    }

    memcpy(destination, source, copy_length);
    destination[copy_length] = '\0';

    if(source_length > copy_length && copy_length > 1) {
        destination[copy_length - 1] = '.';
    }
}

static size_t ai_dashboard_split_line(char* line, char** fields, size_t max_fields) {
    if(max_fields == 0) {
        return 0;
    }

    size_t count = 0;
    fields[count++] = line;

    for(char* cursor = line; *cursor && count < max_fields; cursor++) {
        if(*cursor == '|') {
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
    }

    return count;
}

static void ai_dashboard_snapshot_init(AiDashboardSnapshot* snapshot) {
    memset(snapshot, 0, sizeof(AiDashboardSnapshot));
    ai_dashboard_str_copy(snapshot->updated_at, sizeof(snapshot->updated_at), "No sync yet");
}

static void ai_dashboard_settings_init(AiDashboardSettings* settings) {
    memset(settings, 0, sizeof(AiDashboardSettings));
    settings->autoscroll = false;
    settings->never_dim = false;
}

static int32_t ai_dashboard_find_preference(
    AiDashboardSettings* settings,
    const char* provider_id,
    bool create_if_missing) {
    for(size_t index = 0; index < settings->preference_count; index++) {
        if(strcmp(settings->preferences[index].provider_id, provider_id) == 0) {
            return (int32_t)index;
        }
    }

    if(create_if_missing && settings->preference_count < AI_DASHBOARD_MAX_PROVIDERS) {
        size_t index = settings->preference_count++;
        ai_dashboard_str_copy(
            settings->preferences[index].provider_id,
            sizeof(settings->preferences[index].provider_id),
            provider_id);
        settings->preferences[index].main_mode = 0;
        return (int32_t)index;
    }

    return -1;
}

static bool ai_dashboard_is_visible_on_main(AiDashboardModel* model, const char* provider_id) {
    int32_t index = ai_dashboard_find_preference(&model->settings, provider_id, false);
    if(index < 0) {
        return true;
    }

    return model->settings.preferences[index].main_mode != 2;
}

static uint8_t ai_dashboard_main_mode(AiDashboardModel* model, const char* provider_id) {
    int32_t index = ai_dashboard_find_preference(&model->settings, provider_id, false);
    if(index < 0) {
        return 0;
    }

    return model->settings.preferences[index].main_mode;
}

typedef struct {
    uint8_t used;
    const char* reset;
    const char* label;
} AiDashboardWindow;

static AiDashboardWindow ai_dashboard_window_for(AiDashboardProvider* provider, bool weekly) {
    AiDashboardWindow w;
    if(weekly) {
        w.used = provider->weekly_used;
        w.reset = provider->weekly_reset;
        w.label = "Weekly";
    } else {
        w.used = provider->short_used;
        w.reset = provider->short_reset;
        w.label = provider->short_label[0] != '\0' ? provider->short_label : "5h";
    }
    return w;
}

static bool ai_dashboard_weekly_active(AiDashboardModel* model, AiDashboardProvider* provider) {
    return ai_dashboard_main_mode(model, provider->id) == 1;
}

static AiDashboardWindow ai_dashboard_active_window(
    AiDashboardModel* model,
    AiDashboardProvider* provider) {
    return ai_dashboard_window_for(provider, ai_dashboard_weekly_active(model, provider));
}

static size_t ai_dashboard_visible_provider_count(AiDashboardModel* model) {
    size_t count = 0;

    for(size_t index = 0; index < model->snapshot.provider_count; index++) {
        if(ai_dashboard_is_visible_on_main(model, model->snapshot.providers[index].id)) {
            count++;
        }
    }

    return count;
}

static int32_t ai_dashboard_visible_provider_at(AiDashboardModel* model, size_t visible_index) {
    size_t seen = 0;

    for(size_t index = 0; index < model->snapshot.provider_count; index++) {
        if(!ai_dashboard_is_visible_on_main(model, model->snapshot.providers[index].id)) {
            continue;
        }

        if(seen == visible_index) {
            return (int32_t)index;
        }

        seen++;
    }

    return -1;
}

static uint8_t ai_dashboard_max_scroll(AiDashboardModel* model) {
    size_t visible = ai_dashboard_visible_provider_count(model);
    if(visible <= AI_DASHBOARD_MAIN_ROWS) {
        return 0;
    }

    return (uint8_t)(visible - AI_DASHBOARD_MAIN_ROWS);
}

static uint8_t ai_dashboard_detail_max_scroll(AiDashboardModel* model) {
    if(model->screen_index == 0 || model->screen_index > model->snapshot.provider_count) {
        return 0;
    }

    return 1; // two windows -> always a second detail screen
}

static void ai_dashboard_clamp_state(AiDashboardModel* model) {
    size_t total_screens = model->snapshot.provider_count + 1;
    if(total_screens == 0) {
        total_screens = 1;
    }

    if(model->screen_index >= total_screens) {
        model->screen_index = 0;
    }

    uint8_t max_scroll = ai_dashboard_max_scroll(model);
    if(model->main_scroll > max_scroll) {
        model->main_scroll = max_scroll;
    }

    uint8_t detail_max_scroll = ai_dashboard_detail_max_scroll(model);
    if(model->detail_scroll > detail_max_scroll) {
        model->detail_scroll = detail_max_scroll;
    }
}

static void ai_dashboard_load_snapshot(Storage* storage, AiDashboardSnapshot* snapshot) {
    ai_dashboard_snapshot_init(snapshot);

    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();

    if(file_stream_open(stream, APP_DATA_PATH("usage.txt"), FSAM_READ, FSOM_OPEN_EXISTING)) {
        while(stream_read_line(stream, line)) {
            furi_string_replace_all(line, "\r", "");
            furi_string_replace_all(line, "\n", "");

            char buffer[196];
            ai_dashboard_str_copy(buffer, sizeof(buffer), furi_string_get_cstr(line));
            if(buffer[0] == '\0') {
                continue;
            }

            char* fields[AI_DASHBOARD_MAX_FIELDS];
            size_t field_count = ai_dashboard_split_line(buffer, fields, AI_DASHBOARD_MAX_FIELDS);
            if(field_count < 2) {
                continue;
            }

            if(strcmp(fields[0], "meta") == 0) {
                ai_dashboard_str_copy(snapshot->updated_at, sizeof(snapshot->updated_at), fields[1]);
                continue;
            }

            if(strcmp(fields[0], "provider") == 0 && field_count >= 10 &&
               snapshot->provider_count < AI_DASHBOARD_MAX_PROVIDERS) {
                AiDashboardProvider* provider = &snapshot->providers[snapshot->provider_count++];
                memset(provider, 0, sizeof(AiDashboardProvider));

                ai_dashboard_str_copy(provider->id, sizeof(provider->id), fields[1]);
                ai_dashboard_str_copy(provider->name, sizeof(provider->name), fields[2]);
                ai_dashboard_str_copy(provider->icon, sizeof(provider->icon), fields[3]);
                ai_dashboard_str_copy(provider->source, sizeof(provider->source), fields[4]);
                ai_dashboard_str_copy(
                    provider->short_label, sizeof(provider->short_label), fields[5]);
                provider->short_used = (uint8_t)AI_DASHBOARD_CLAMP(atoi(fields[6]), 0, 100);
                ai_dashboard_str_copy(
                    provider->short_reset, sizeof(provider->short_reset), fields[7]);
                provider->weekly_used = (uint8_t)AI_DASHBOARD_CLAMP(atoi(fields[8]), 0, 100);
                ai_dashboard_str_copy(
                    provider->weekly_reset, sizeof(provider->weekly_reset), fields[9]);
                provider->loaded = true;

                if(provider->name[0] == '\0') {
                    ai_dashboard_str_copy(provider->name, sizeof(provider->name), provider->id);
                }
                if(provider->icon[0] == '\0') {
                    ai_dashboard_str_copy(provider->icon, sizeof(provider->icon), "AI");
                }
                if(provider->short_label[0] == '\0') {
                    ai_dashboard_str_copy(
                        provider->short_label, sizeof(provider->short_label), "5h");
                }
            }
        }
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_string_free(line);
}

static void ai_dashboard_load_settings(Storage* storage, AiDashboardSettings* settings) {
    ai_dashboard_settings_init(settings);

    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();

    if(file_stream_open(stream, APP_DATA_PATH("settings.txt"), FSAM_READ, FSOM_OPEN_EXISTING)) {
        while(stream_read_line(stream, line)) {
            furi_string_replace_all(line, "\r", "");
            furi_string_replace_all(line, "\n", "");

            char buffer[128];
            ai_dashboard_str_copy(buffer, sizeof(buffer), furi_string_get_cstr(line));
            if(buffer[0] == '\0') {
                continue;
            }

            char* fields[4];
            size_t field_count = ai_dashboard_split_line(buffer, fields, 4);
            if(field_count < 2) {
                continue;
            }

            if(strcmp(fields[0], "autoscroll") == 0) {
                settings->autoscroll = atoi(fields[1]) != 0;
                continue;
            }

            if(strcmp(fields[0], "never_dim") == 0) {
                settings->never_dim = atoi(fields[1]) != 0;
                continue;
            }

            if(strcmp(fields[0], "main") == 0 && field_count >= 3) {
                int32_t index = ai_dashboard_find_preference(settings, fields[1], true);
                if(index >= 0) {
                    settings->preferences[index].main_mode = atoi(fields[2]) != 0 ? 0 : 2;
                }
            }

            if(strcmp(fields[0], "main_v2") == 0 && field_count >= 3) {
                int32_t index = ai_dashboard_find_preference(settings, fields[1], true);
                if(index >= 0) {
                    settings->preferences[index].main_mode =
                        (uint8_t)AI_DASHBOARD_CLAMP(atoi(fields[2]), 0, 2);
                }
            }
        }
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_string_free(line);
}

static void ai_dashboard_save_settings(Storage* storage, const AiDashboardSettings* settings) {
    File* file = storage_file_alloc(storage);
    FuriString* output = furi_string_alloc();

    furi_string_cat_printf(output, "autoscroll|%u\n", settings->autoscroll ? 1 : 0);
    furi_string_cat_printf(output, "never_dim|%u\n", settings->never_dim ? 1 : 0);
    for(size_t index = 0; index < settings->preference_count; index++) {
        furi_string_cat_printf(
            output,
            "main_v2|%s|%u\n",
            settings->preferences[index].provider_id,
            settings->preferences[index].main_mode);
    }

    if(storage_file_open(file, APP_DATA_PATH("settings.txt"), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, furi_string_get_cstr(output), furi_string_size(output));
        storage_file_close(file);
    }

    furi_string_free(output);
    storage_file_free(file);
}

static bool ai_dashboard_save_snapshot_text(Storage* storage, const char* snapshot_text) {
    bool saved = false;
    File* file = storage_file_alloc(storage);

    if(storage_file_open(file, APP_DATA_PATH("usage.txt"), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        size_t length = strlen(snapshot_text);
        saved = storage_file_write(file, snapshot_text, length) == length;
        storage_file_close(file);
    }

    storage_file_free(file);
    return saved;
}

static void ai_dashboard_refresh_model(AiDashboardApp* app, AiDashboardModel* model) {
    ai_dashboard_load_snapshot(app->storage, &model->snapshot);

    for(size_t index = 0; index < model->snapshot.provider_count; index++) {
        ai_dashboard_find_preference(&model->settings, model->snapshot.providers[index].id, true);
    }

    ai_dashboard_clamp_state(model);
}

static float ai_dashboard_progress_value(uint8_t used_percent) {
    uint8_t clamped = used_percent > 100 ? 100 : used_percent;
    return ((float)clamped) / 100.0f;
}

static uint8_t ai_dashboard_remaining_percent(uint8_t used_percent) {
    uint8_t clamped = used_percent > 100 ? 100 : used_percent;
    return 100 - clamped;
}

static void ai_dashboard_short_timestamp(char* destination, size_t destination_size, const char* timestamp) {
    const char* value = timestamp;
    const char* separator = strrchr(timestamp, ' ');
    if(separator && separator[1]) {
        value = separator + 1;
    }
    ai_dashboard_str_truncate(destination, destination_size, value, 5);
}

static const Icon* ai_dashboard_provider_icon(AiDashboardProvider* provider) {
    if(strcmp(provider->id, "chatgpt") == 0) {
        return &I_chatgpt_12x12;
    } else if(strcmp(provider->id, "claude") == 0) {
        return &I_claude_12x12;
    } else if(strcmp(provider->id, "codex") == 0) {
        return &I_codex_12x12;
    } else if(strcmp(provider->id, "cursor") == 0) {
        return &I_cursor_12x12;
    } else if(strcmp(provider->id, "gemini") == 0) {
        return &I_gemini_12x12;
    } else {
        return &I_bot_12x12;
    }
}

static const Icon* ai_dashboard_provider_icon_small(AiDashboardProvider* provider) {
    if(strcmp(provider->id, "chatgpt") == 0) {
        return &I_chatgpt_10x10;
    } else if(strcmp(provider->id, "claude") == 0) {
        return &I_claude_10x10;
    } else if(strcmp(provider->id, "codex") == 0) {
        return &I_codex_10x10;
    } else if(strcmp(provider->id, "cursor") == 0) {
        return &I_cursor_10x10;
    } else if(strcmp(provider->id, "gemini") == 0) {
        return &I_gemini_10x10;
    } else {
        return &I_bot_10x10;
    }
}

static void ai_dashboard_draw_provider_icon(Canvas* canvas, int32_t x, int32_t y, AiDashboardProvider* provider) {
    canvas_draw_icon(canvas, x, y, ai_dashboard_provider_icon(provider));
}

static void ai_dashboard_draw_provider_icon_small(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    AiDashboardProvider* provider) {
    canvas_draw_icon(canvas, x, y, ai_dashboard_provider_icon_small(provider));
}

static void ai_dashboard_apply_display_mode(AiDashboardApp* app, bool never_dim) {
    if(never_dim) {
        notification_message(app->notification, &sequence_display_backlight_enforce_on);
        notification_message(app->notification, &sequence_display_backlight_on);
    } else {
        notification_message(app->notification, &sequence_display_backlight_enforce_auto);
    }
}

static void ai_dashboard_ble_feed(AiDashboardApp* app, const uint8_t* data, size_t size) {
    if(!app || !app->ble_mutex || !app->view_dispatcher || size == 0) {
        return;
    }

    bool snapshot_ready = false;
    app->ble_debug_chunks++;
    app->ble_debug_bytes += (uint32_t)size;

    furi_check(furi_mutex_acquire(app->ble_mutex, FuriWaitForever) == FuriStatusOk);

    size_t remaining = AI_DASHBOARD_BLE_BUFFER_SIZE - app->ble_rx_size - 1;
    if(size > remaining) {
        app->ble_rx_size = 0;
        app->ble_rx_buffer[0] = '\0';
        furi_mutex_release(app->ble_mutex);
        ai_dashboard_ble_debug_write(app, "chunk_overflow");
        return;
    }

    memcpy(app->ble_rx_buffer + app->ble_rx_size, data, size);
    app->ble_rx_size += size;
    app->ble_rx_buffer[app->ble_rx_size] = '\0';

    char* marker = strstr(app->ble_rx_buffer, ai_dashboard_ble_end_marker);
    if(marker) {
        app->ble_debug_markers++;
        *marker = '\0';
        strlcpy(app->ble_snapshot_buffer, app->ble_rx_buffer, sizeof(app->ble_snapshot_buffer));
        app->ble_snapshot_ready = true;
        snapshot_ready = true;
        app->ble_rx_size = 0;
        app->ble_rx_buffer[0] = '\0';
    }

    furi_mutex_release(app->ble_mutex);
    if(snapshot_ready) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, AiDashboardCustomEventBleSnapshotUpdated);
    }
    ai_dashboard_ble_debug_write(app, snapshot_ready ? "marker" : "chunk");
}

// Host -> Flipper App Bridge commands. Runs on the BT/GAP thread.
static void ai_dashboard_app_bridge_rx(const void* message, void* context) {
    AiDashboardApp* app = context;
    const BtAppBridgeEvent* event = message;
    if(!app || !event) {
        return;
    }
    if(strcmp(event->app_id, AI_DASHBOARD_APP_ID) != 0) {
        return;
    }
    ai_dashboard_ble_feed(app, event->payload, event->payload_len);
}

static void ai_dashboard_ble_request_refresh(AiDashboardApp* app) {
    if(!app || !app->bt) {
        return;
    }
    bt_app_bridge_send_text(app->bt, AI_DASHBOARD_APP_ID, "refresh", "");
    app->ble_debug_registers++;
    ai_dashboard_ble_debug_write(app, "refresh_sent");
    // Visual + haptic confirmation that the request was sent.
    if(app->notification) {
        notification_message(app->notification, &sequence_single_vibro);
        notification_message(app->notification, &sequence_blink_blue_100);
    }
}

static void ai_dashboard_ble_start(AiDashboardApp* app) {
    FuriPubSub* pubsub = bt_app_bridge_get_pubsub(app->bt);
    if(pubsub) {
        app->app_bridge_sub = furi_pubsub_subscribe(pubsub, ai_dashboard_app_bridge_rx, app);
        ai_dashboard_ble_debug_write(app, "started");
    }
}

static void ai_dashboard_ble_stop(AiDashboardApp* app) {
    if(app->app_bridge_sub) {
        FuriPubSub* pubsub = bt_app_bridge_get_pubsub(app->bt);
        if(pubsub) {
            furi_pubsub_unsubscribe(pubsub, app->app_bridge_sub);
        }
        app->app_bridge_sub = NULL;
    }
}

static void ai_dashboard_draw_placeholder(Canvas* canvas, const char* headline, const char* subtitle) {
    canvas_draw_icon(canvas, 4, 1, &I_radar_ai_10px);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 20, 10, "AI Radar");
    canvas_draw_line(canvas, 0, 13, 127, 13);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_icon(canvas, 8, 23, &I_bot_12x12);
    canvas_draw_str(canvas, 28, 30, headline);
    canvas_draw_str(canvas, 28, 42, subtitle);
}

static void ai_dashboard_draw_main(Canvas* canvas, AiDashboardModel* model) {
    char page_text[8];
    char time_text[8];

    snprintf(
        page_text,
        sizeof(page_text),
        "%u/%u",
        (unsigned)(model->screen_index + 1),
        (unsigned)(model->snapshot.provider_count + 1));
    ai_dashboard_short_timestamp(time_text, sizeof(time_text), model->snapshot.updated_at);

    canvas_draw_icon(canvas, 2, 1, &I_radar_ai_10px);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 15, 10, "AI Radar");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 98, 10, AlignRight, AlignBottom, time_text);
    canvas_draw_str_aligned(canvas, 122, 10, AlignRight, AlignBottom, page_text);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    size_t visible_count = ai_dashboard_visible_provider_count(model);
    if(model->snapshot.provider_count == 0) {
        ai_dashboard_draw_placeholder(canvas, "No signal", "Run bridge sync first");
        return;
    }

    if(visible_count == 0) {
        ai_dashboard_draw_placeholder(canvas, "All hidden", "Unhide providers in settings");
        return;
    }

    for(size_t row = 0; row < AI_DASHBOARD_MAIN_ROWS; row++) {
        size_t visible_index = model->main_scroll + row;
        if(visible_index >= visible_count) {
            break;
        }

        int32_t provider_index = ai_dashboard_visible_provider_at(model, visible_index);
        if(provider_index < 0) {
            continue;
        }

        AiDashboardProvider* provider = &model->snapshot.providers[provider_index];
        int32_t row_top = 16 + (int32_t)row * 24;
        char full_title[40];
        char main_label[20];
        char title_text[24];
        char used_text[8];
        char left_text[8];
        AiDashboardWindow active = ai_dashboard_active_window(model, provider);
        uint8_t used_percent = active.used;
        uint8_t left_percent = ai_dashboard_remaining_percent(active.used);

        ai_dashboard_draw_provider_icon(canvas, 2, row_top + 3, provider);
        ai_dashboard_str_copy(main_label, sizeof(main_label), active.label);
        snprintf(full_title, sizeof(full_title), "%s - %s", provider->name, main_label);
        ai_dashboard_str_truncate(title_text, sizeof(title_text), full_title, 20);
        snprintf(used_text, sizeof(used_text), "%u%%", used_percent);
        snprintf(left_text, sizeof(left_text), "%u%%", left_percent);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 18, row_top + 7, title_text);

        canvas_draw_str(canvas, 18, row_top + 18, used_text);
        elements_progress_bar(
            canvas, 38, row_top + 10, 48, ai_dashboard_progress_value(active.used));
        canvas_draw_str(canvas, 90, row_top + 18, left_text);
    }

    if(ai_dashboard_max_scroll(model) > 0) {
        elements_scrollbar_pos(
            canvas, 124, 16, 47, model->main_scroll, ai_dashboard_max_scroll(model) + 1);
    }
}

static void ai_dashboard_draw_detail(Canvas* canvas, AiDashboardModel* model) {
    if(model->screen_index == 0 || model->screen_index > model->snapshot.provider_count) {
        ai_dashboard_draw_placeholder(canvas, "No Detail", "Return to overview");
        return;
    }

    AiDashboardProvider* provider = &model->snapshot.providers[model->screen_index - 1];
    char page_text[8];
    char name_text[16];
    char used_text[8];
    char left_text[8];
    char line_one[28];
    char line_two[28];
    char line_three[28];
    AiDashboardWindow detail_active = ai_dashboard_active_window(model, provider);
    AiDashboardWindow detail_other =
        ai_dashboard_window_for(provider, !ai_dashboard_weekly_active(model, provider));
    uint8_t used_percent = detail_active.used;
    uint8_t left_percent = ai_dashboard_remaining_percent(detail_active.used);

    snprintf(
        page_text,
        sizeof(page_text),
        "%u/%u",
        (unsigned)(model->screen_index + 1),
        (unsigned)(model->snapshot.provider_count + 1));
    ai_dashboard_str_truncate(name_text, sizeof(name_text), provider->name, 14);

    line_one[0] = '\0';
    line_two[0] = '\0';
    line_three[0] = '\0';

    {
        AiDashboardWindow show = (model->detail_scroll == 0) ? detail_active : detail_other;
        used_percent = show.used;
        left_percent = ai_dashboard_remaining_percent(show.used);
        snprintf(line_one, sizeof(line_one), "Window: %s", show.label);
        snprintf(line_two, sizeof(line_two), "Reset: %s", show.reset);
    }

    snprintf(used_text, sizeof(used_text), "%u%%", used_percent);
    snprintf(left_text, sizeof(left_text), "%u%%", left_percent);

    ai_dashboard_draw_provider_icon_small(canvas, 4, 2, provider);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 18, 10, name_text);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 122, 10, AlignRight, AlignBottom, page_text);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    canvas_draw_str(canvas, 4, 28, used_text);
    elements_progress_bar(canvas, 24, 20, 72, ai_dashboard_progress_value(used_percent));
    canvas_draw_str_aligned(canvas, 122, 28, AlignRight, AlignBottom, left_text);

    if(line_one[0] != '\0') {
        char clipped[28];
        ai_dashboard_str_truncate(clipped, sizeof(clipped), line_one, 26);
        canvas_draw_str(canvas, 4, 39, clipped);
    }
    if(line_two[0] != '\0') {
        char clipped[28];
        ai_dashboard_str_truncate(clipped, sizeof(clipped), line_two, 26);
        canvas_draw_str(canvas, 4, 50, clipped);
    }
    if(line_three[0] != '\0') {
        char clipped[28];
        ai_dashboard_str_truncate(clipped, sizeof(clipped), line_three, 26);
        canvas_draw_str(canvas, 4, 61, clipped);
    }
}

static void ai_dashboard_draw_callback(Canvas* canvas, void* model_ptr) {
    AiDashboardModel* model = model_ptr;
    canvas_clear(canvas);

    if(model->screen_index == 0) {
        ai_dashboard_draw_main(canvas, model);
    } else {
        ai_dashboard_draw_detail(canvas, model);
    }
}

static bool ai_dashboard_navigation_callback(void* context) {
    AiDashboardApp* app = context;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static uint32_t ai_dashboard_settings_previous_callback(void* context) {
    UNUSED(context);
    return AiDashboardViewDashboard;
}

static void ai_dashboard_autoscroll_changed(VariableItem* item) {
    AiDashboardApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    bool enabled = index != 0;

    variable_item_set_current_value_text(item, ai_dashboard_on_off_text[enabled ? 1 : 0]);

    AiDashboardSettings settings_copy;
    memset(&settings_copy, 0, sizeof(settings_copy));

    with_view_model(
        app->dashboard_view,
        AiDashboardModel * model,
        {
            model->settings.autoscroll = enabled;
            settings_copy = model->settings;
        },
        false);

    ai_dashboard_save_settings(app->storage, &settings_copy);
}

static void ai_dashboard_never_dim_changed(VariableItem* item) {
    AiDashboardApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    bool enabled = index != 0;

    variable_item_set_current_value_text(item, ai_dashboard_on_off_text[enabled ? 1 : 0]);

    AiDashboardSettings settings_copy;
    memset(&settings_copy, 0, sizeof(settings_copy));

    with_view_model(
        app->dashboard_view,
        AiDashboardModel * model,
        {
            model->settings.never_dim = enabled;
            settings_copy = model->settings;
        },
        false);

    ai_dashboard_apply_display_mode(app, enabled);
    ai_dashboard_save_settings(app->storage, &settings_copy);
}

static void ai_dashboard_provider_visibility_changed(VariableItem* item) {
    AiDashboardToggleBinding* binding = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    variable_item_set_current_value_text(item, ai_dashboard_toggle_text[index]);

    AiDashboardSettings settings_copy;
    memset(&settings_copy, 0, sizeof(settings_copy));

    with_view_model(
        binding->app->dashboard_view,
        AiDashboardModel * model,
        {
            if(binding->preference_index < model->settings.preference_count) {
                model->settings.preferences[binding->preference_index].main_mode = index;
                ai_dashboard_clamp_state(model);
                settings_copy = model->settings;
            }
        },
        true);

    ai_dashboard_save_settings(binding->app->storage, &settings_copy);
}

static void ai_dashboard_info_item_changed(VariableItem* item) {
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, "@T-Damer");
}

static void ai_dashboard_apply_settings_callbacks(AiDashboardApp* app) {
    variable_item_list_reset(app->settings_list);

    AiDashboardModel snapshot_model;
    memset(&snapshot_model, 0, sizeof(snapshot_model));

    with_view_model(
        app->dashboard_view,
        AiDashboardModel * model,
        {
            snapshot_model = *model;
        },
        false);

    VariableItem* item = variable_item_list_add(
        app->settings_list, "Autoscroll", 2, ai_dashboard_autoscroll_changed, app);
    variable_item_set_current_value_index(item, snapshot_model.settings.autoscroll ? 1 : 0);
    variable_item_set_current_value_text(
        item, ai_dashboard_on_off_text[snapshot_model.settings.autoscroll ? 1 : 0]);

    item = variable_item_list_add(
        app->settings_list, "Never dim", 2, ai_dashboard_never_dim_changed, app);
    variable_item_set_current_value_index(item, snapshot_model.settings.never_dim ? 1 : 0);
    variable_item_set_current_value_text(
        item, ai_dashboard_on_off_text[snapshot_model.settings.never_dim ? 1 : 0]);

    for(size_t index = 0; index < snapshot_model.snapshot.provider_count; index++) {
        AiDashboardProvider* provider = &snapshot_model.snapshot.providers[index];
        int32_t preference_index =
            ai_dashboard_find_preference(&snapshot_model.settings, provider->id, false);
        if(preference_index < 0) {
            continue;
        }

        ai_dashboard_str_copy(
            app->provider_labels[index],
            sizeof(app->provider_labels[index]),
            provider->name);
        app->toggle_bindings[index].app = app;
        app->toggle_bindings[index].preference_index = (size_t)preference_index;

        item = variable_item_list_add(
            app->settings_list,
            app->provider_labels[index],
            3,
            ai_dashboard_provider_visibility_changed,
            &app->toggle_bindings[index]);

        uint8_t main_mode = snapshot_model.settings.preferences[preference_index].main_mode;
        variable_item_set_current_value_index(item, main_mode);
        variable_item_set_current_value_text(item, ai_dashboard_toggle_text[main_mode]);
    }

    item = variable_item_list_add(app->settings_list, "GitHub", 1, ai_dashboard_info_item_changed, app);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, "@T-Damer");
}

static bool ai_dashboard_custom_event_callback(void* context, uint32_t event) {
    AiDashboardApp* app = context;

    if(event == AiDashboardCustomEventOpenSettings) {
        with_view_model(
            app->dashboard_view,
            AiDashboardModel * model,
            {
                ai_dashboard_refresh_model(app, model);
            },
            true);
        ai_dashboard_apply_settings_callbacks(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, AiDashboardViewSettings);
        return true;
    }

    if(event == AiDashboardCustomEventBleSnapshotUpdated) {
        char snapshot_text[AI_DASHBOARD_BLE_BUFFER_SIZE];
        bool has_snapshot = false;

        furi_check(furi_mutex_acquire(app->ble_mutex, FuriWaitForever) == FuriStatusOk);
        if(app->ble_snapshot_ready) {
            ai_dashboard_str_copy(snapshot_text, sizeof(snapshot_text), app->ble_snapshot_buffer);
            app->ble_snapshot_ready = false;
            has_snapshot = true;
        }
        furi_mutex_release(app->ble_mutex);

        if(has_snapshot && ai_dashboard_save_snapshot_text(app->storage, snapshot_text)) {
            app->ble_debug_saves++;
            ai_dashboard_ble_debug_write(app, "saved");
            with_view_model(
                app->dashboard_view,
                AiDashboardModel * model,
                {
                    ai_dashboard_refresh_model(app, model);
                },
                true);
        } else if(has_snapshot) {
            ai_dashboard_ble_debug_write(app, "save_failed");
        }
        return true;
    }

    return false;
}

static void ai_dashboard_tick_callback(void* context) {
    AiDashboardApp* app = context;

    with_view_model(
        app->dashboard_view,
        AiDashboardModel * model,
        {
            model->tick_count++;

            if((model->tick_count % 6) == 0) {
                ai_dashboard_refresh_model(app, model);
            }

            if(model->screen_index == 0 && model->settings.autoscroll &&
               ai_dashboard_max_scroll(model) > 0 && (model->tick_count % 4) == 0) {
                uint8_t max_scroll = ai_dashboard_max_scroll(model);
                if(model->main_scroll >= max_scroll) {
                    model->main_scroll = 0;
                } else {
                    model->main_scroll++;
                }
            }
        },
        true);
}

static bool ai_dashboard_input_callback(InputEvent* event, void* context) {
    AiDashboardApp* app = context;
    bool consumed = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat &&
       event->type != InputTypeLong) {
        return false;
    }

    with_view_model(
        app->dashboard_view,
        AiDashboardModel * model,
        {
            size_t total_screens = model->snapshot.provider_count + 1;
            if(total_screens == 0) {
                total_screens = 1;
            }

            if(event->key == InputKeyLeft) {
                if(model->screen_index == 0) {
                    model->screen_index = (uint8_t)(total_screens - 1);
                } else {
                    model->screen_index--;
                }
                model->detail_scroll = 0;
                consumed = true;
            } else if(event->key == InputKeyRight) {
                model->screen_index = (uint8_t)((model->screen_index + 1) % total_screens);
                model->detail_scroll = 0;
                consumed = true;
            } else if(event->key == InputKeyUp && model->screen_index == 0) {
                if(model->main_scroll > 0) {
                    model->main_scroll--;
                    consumed = true;
                }
            } else if(event->key == InputKeyUp && model->screen_index > 0) {
                if(model->detail_scroll > 0) {
                    model->detail_scroll--;
                    consumed = true;
                }
            } else if(event->key == InputKeyDown && model->screen_index == 0) {
                uint8_t max_scroll = ai_dashboard_max_scroll(model);
                if(model->main_scroll < max_scroll) {
                    model->main_scroll++;
                    consumed = true;
                }
            } else if(event->key == InputKeyDown && model->screen_index > 0) {
                uint8_t detail_max_scroll = ai_dashboard_detail_max_scroll(model);
                if(model->detail_scroll < detail_max_scroll) {
                    model->detail_scroll++;
                    consumed = true;
                }
            }

            ai_dashboard_clamp_state(model);
        },
        consumed);

    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, AiDashboardCustomEventOpenSettings);
        consumed = true;
    }

    if(event->key == InputKeyOk && event->type == InputTypeLong) {
        ai_dashboard_ble_request_refresh(app);
        consumed = true;
    }

    return consumed;
}

static AiDashboardApp* ai_dashboard_alloc(void) {
    AiDashboardApp* app = malloc(sizeof(AiDashboardApp));
    memset(app, 0, sizeof(AiDashboardApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->bt = furi_record_open(RECORD_BT);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->ble_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, ai_dashboard_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, ai_dashboard_navigation_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, ai_dashboard_tick_callback, 500);

    app->dashboard_view = view_alloc();
    view_set_context(app->dashboard_view, app);
    view_allocate_model(app->dashboard_view, ViewModelTypeLocking, sizeof(AiDashboardModel));
    view_set_draw_callback(app->dashboard_view, ai_dashboard_draw_callback);
    view_set_input_callback(app->dashboard_view, ai_dashboard_input_callback);

    with_view_model(
        app->dashboard_view,
        AiDashboardModel * model,
        {
            memset(model, 0, sizeof(AiDashboardModel));
            ai_dashboard_load_settings(app->storage, &model->settings);
            ai_dashboard_refresh_model(app, model);
            ai_dashboard_apply_display_mode(app, model->settings.never_dim);
        },
        true);

    app->settings_list = variable_item_list_alloc();
    View* settings_view = variable_item_list_get_view(app->settings_list);
    view_set_previous_callback(settings_view, ai_dashboard_settings_previous_callback);

    ai_dashboard_apply_settings_callbacks(app);

    view_dispatcher_add_view(
        app->view_dispatcher, AiDashboardViewDashboard, app->dashboard_view);
    view_dispatcher_add_view(app->view_dispatcher, AiDashboardViewSettings, settings_view);
    ai_dashboard_ble_start(app);

    return app;
}

static void ai_dashboard_free(AiDashboardApp* app) {
    ai_dashboard_apply_display_mode(app, false);
    ai_dashboard_ble_stop(app);
    view_dispatcher_remove_view(app->view_dispatcher, AiDashboardViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, AiDashboardViewDashboard);

    variable_item_list_free(app->settings_list);
    view_free(app->dashboard_view);
    view_dispatcher_free(app->view_dispatcher);

    furi_mutex_free(app->ble_mutex);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t ai_dashboard_app(void* p) {
    UNUSED(p);

    AiDashboardApp* app = ai_dashboard_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, AiDashboardViewDashboard);
    view_dispatcher_run(app->view_dispatcher);
    ai_dashboard_free(app);

    return 0;
}
