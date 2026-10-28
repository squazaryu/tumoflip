#include "power_history.h"
#include "power_store.h"

#include <dialogs/dialogs.h>
#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <power/power_service/power.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {PowerViewMenu, PowerViewDisplay, PowerViewHelp};

typedef struct {
    PowerHistorySample sample;
    uint32_t rejected;
    uint16_t capacity;
    uint8_t segments;
    uint8_t graph[64];
    uint8_t graph_count;
    uint8_t page;
    bool estimate;
    bool sample_ok;
    bool store_ok;
    bool paused;
    bool writable;
} PowerJournalView;

typedef struct {
    PowerHistory history;
    Power* power;
    Storage* storage;
    Gui* gui;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    View* view;
    Submenu* menu;
    TextBox* help;
    uint32_t sampled_at;
    uint8_t since_save;
    uint8_t page;
    bool sample_ok;
    bool store_ok;
    bool paused;
    bool writable;
} PowerJournalApp;

static void power_journal_refresh(PowerJournalApp* app) {
    PowerJournalView snapshot = {
        .page = app->page,
        .sample_ok = app->sample_ok,
        .store_ok = app->store_ok,
        .paused = app->paused,
        .writable = app->writable,
        .segments = app->history.estimate_count,
        .rejected = app->history.rejected,
    };
    const PowerHistorySample* sample = app->history.count ?
        power_history_at(&app->history, app->history.count - 1U) : NULL;
    if(sample) snapshot.sample = *sample;
    snapshot.estimate = power_history_estimate(&app->history, &snapshot.capacity);
    snapshot.graph_count = app->history.count < 64U ? app->history.count : 64U;
    for(unsigned i = 0; i < snapshot.graph_count; i++) {
        const unsigned index = app->history.count - snapshot.graph_count + i;
        snapshot.graph[i] = power_history_at(&app->history, index)->soc;
    }
    with_view_model(app->view, PowerJournalView * model, { *model = snapshot; }, true);
}

static void power_journal_draw(Canvas* canvas, void* context) {
    const PowerJournalView* model = context;
    char text[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 8, "Power Journal");
    const char* state = !model->writable ? "READONLY" : model->paused ? "PAUSED" :
                        !model->sample_ok ? "INVALID" : "APP-ONLY";
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, state);
    canvas_draw_line(canvas, 0, 10, 127, 10);
    if(model->page == 0) {
        snprintf(text, sizeof(text), "%u", model->sample.soc);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 25, 29, AlignCenter, AlignCenter, text);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 48, 32, "%");
        snprintf(text, sizeof(text), "%u.%03u V", model->sample.voltage_mv / 1000U,
                 model->sample.voltage_mv % 1000U);
        canvas_draw_str_aligned(canvas, 127, 23, AlignRight, AlignBottom, text);
        snprintf(text, sizeof(text), "%ld mA", (long)model->sample.current_ma);
        canvas_draw_str_aligned(canvas, 127, 33, AlignRight, AlignBottom, text);
        const int temperature = model->sample.temperature_tenths;
        snprintf(text, sizeof(text), "%s%d.%d C", temperature < 0 ? "-" : "",
                 abs(temperature) / 10, abs(temperature) % 10);
        canvas_draw_str_aligned(canvas, 127, 43, AlignRight, AlignBottom, text);
        snprintf(text, sizeof(text), "Gauge %u%%", model->sample.gauge_health);
        canvas_draw_str(canvas, 1, 51, text);
        snprintf(text, sizeof(text), "Heap %luK", (unsigned long)(model->sample.heap_free / 1024U));
        canvas_draw_str_aligned(canvas, 127, 51, AlignRight, AlignBottom, text);
    } else if(model->page == 1) {
        canvas_draw_str(canvas, 1, 20, "State of charge (%)");
        for(unsigned i = 0; i < model->graph_count; i++) {
            const unsigned x = 1U + i * 2U;
            const unsigned y = 43U - model->graph[i] * 20U / 100U;
            canvas_draw_line(canvas, x, 43, x, y);
        }
        snprintf(text, sizeof(text), "%u app samples / SD:%s", model->graph_count,
                 model->store_ok ? "OK" : "ERR");
        canvas_draw_str(canvas, 1, 51, text);
    } else {
        canvas_set_font(canvas, FontPrimary);
        snprintf(text, sizeof(text), "Gauge: %u%%", model->sample.gauge_health);
        canvas_draw_str(canvas, 1, 24, text);
        canvas_set_font(canvas, FontSecondary);
        if(model->estimate) snprintf(text, sizeof(text), "Observed: %u mAh", model->capacity);
        else snprintf(text, sizeof(text), "Need 3 good segments");
        canvas_draw_str(canvas, 1, 36, text);
        snprintf(text, sizeof(text), "Seg:%u Reject:%lu", model->segments,
                 (unsigned long)model->rejected);
        canvas_draw_str(canvas, 1, 48, text);
    }
    elements_button_left(canvas, "Prev");
    elements_button_center(canvas, "Menu");
    elements_button_right(canvas, "Next");
}

static uint32_t power_journal_previous(void* context) {
    UNUSED(context);
    return PowerViewMenu;
}

static uint32_t power_journal_exit(void* context) {
    PowerJournalApp* app = context;
    power_history_pause(&app->history);
    return VIEW_NONE;
}

static bool power_journal_input(InputEvent* event, void* context) {
    PowerJournalApp* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        app->page = (app->page + (event->key == InputKeyRight ? 1U : 2U)) % 3U;
        power_journal_refresh(app);
        return true;
    }
    if(event->key == InputKeyOk) {
        view_dispatcher_switch_to_view(app->dispatcher, PowerViewMenu);
        return true;
    }
    return false;
}

static void power_journal_tick(void* context) {
    PowerJournalApp* app = context;
    const uint32_t now = furi_get_tick();
    if(app->paused || now - app->sampled_at < 5000U) return;
    app->sampled_at = now;
    PowerInfo info;
    power_get_info(app->power, &info);
    app->sample_ok = info.gauge_is_ok && isfinite(info.voltage_gauge) &&
                     isfinite(info.current_gauge) && isfinite(info.temperature_gauge) &&
                     isfinite(info.voltage_vbus) && info.voltage_gauge >= 2.5f &&
                     info.voltage_gauge <= 4.5f && info.current_gauge >= -5.0f &&
                     info.current_gauge <= 5.0f && info.temperature_gauge >= -20.0f &&
                     info.temperature_gauge <= 60.0f;
    if(app->sample_ok) {
        PowerHistorySample sample;
        memset(&sample, 0, sizeof(sample));
        sample.timestamp = furi_hal_rtc_get_timestamp();
        sample.tick_ms = now;
        sample.heap_free = memmgr_get_free_heap();
        sample.soc = info.charge;
        sample.gauge_health = info.health;
        sample.gauge_ok = 1;
        sample.charging = info.is_charging;
        sample.usb = info.voltage_vbus >= 4.0f;
        sample.voltage_mv = (uint16_t)(info.voltage_gauge * 1000.0f);
        sample.current_ma = (int32_t)(info.current_gauge * 1000.0f);
        sample.temperature_tenths = (int16_t)(info.temperature_gauge * 10.0f);
        app->sample_ok = power_history_add(&app->history, &sample);
    } else {
        power_history_pause(&app->history);
    }
    if(app->sample_ok && ++app->since_save >= 12U) {
        app->since_save = 0;
        if(app->writable) app->store_ok = power_store_save(app->storage, &app->history);
    }
    power_journal_refresh(app);
}

static void power_journal_menu(void* context, uint32_t index) {
    PowerJournalApp* app = context;
    if(index < 3U) {
        app->page = index;
        power_journal_refresh(app);
        view_dispatcher_switch_to_view(app->dispatcher, PowerViewDisplay);
    } else if(index == 3U) {
        char path[256];
        const bool ok = power_store_export(app->storage, &app->history, path, sizeof(path));
        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_header(message, ok ? "CSV verified" : "Export failed", 64, 4,
                                  AlignCenter, AlignTop);
        dialog_message_set_text(message, ok ? "Saved in Device Library\npower folder.\nOriginal files unchanged." :
            "No valid samples or\nSD write/readback failed.", 64, 32, AlignCenter, AlignCenter);
        dialog_message_set_buttons(message, NULL, "OK", NULL);
        dialog_message_show(app->dialogs, message);
        dialog_message_free(message);
    } else if(index == 4U) {
        app->paused = !app->paused;
        power_history_pause(&app->history);
        power_journal_refresh(app);
    } else {
        view_dispatcher_switch_to_view(app->dispatcher, PowerViewHelp);
    }
}

int32_t power_journal_app(void* context) {
    UNUSED(context);
    PowerJournalApp* app = calloc(1, sizeof(*app));
    if(!app) return -1;
    app->power = furi_record_open(RECORD_POWER);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    const PowerStoreStatus loaded = power_store_load(app->storage, &app->history);
    app->writable = loaded != PowerStoreError;
    app->store_ok = app->writable;
    app->sampled_at = furi_get_tick() - 5000U;
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->help = text_box_alloc();
    app->view = view_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->dispatcher, power_journal_tick, 1000U);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(PowerJournalView));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, power_journal_draw);
    view_set_input_callback(app->view, power_journal_input);
    view_set_previous_callback(app->view, power_journal_previous);
    view_set_previous_callback(submenu_get_view(app->menu), power_journal_exit);
    view_set_previous_callback(text_box_get_view(app->help), power_journal_previous);
    submenu_set_header(app->menu, "Power Journal");
    const char* labels[] = {"Live battery", "History graph", "Capacity evidence", "Export checked CSV",
                           "Pause / resume", "Scope / storage"};
    for(unsigned i = 0; i < COUNT_OF(labels); i++) {
        submenu_add_item(app->menu, labels[i], i, power_journal_menu, app);
    }
    text_box_set_text(app->help,
        "APP-ONLY history\n\nSamples every5 seconds\nonly while this FAP\nis open. No idle or\nbackground guarantee.\n\n"
        "Charge control is OFF.\nGauge health and our\ncapacity estimate are\ndifferent observations.\nNot laboratory accuracy.\n\n"
        "Capacity: 3 qualified\n15pct discharge segments,\nno USB/charge/gaps.\nReopening resets only\nunfinished inference.\n\n"
        "Four checked SD slots,\n128 recent samples,\n16 capacity candidates.\nCorrupt storage opens\nread-only, never erased.\n\n"
        "CSV exports are new\nverified files under\napps_data/device_library\n/power. Copy via SD/USB\nreader; no user signal\nis changed or deleted.");
    view_dispatcher_add_view(app->dispatcher, PowerViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, PowerViewDisplay, app->view);
    view_dispatcher_add_view(app->dispatcher, PowerViewHelp, text_box_get_view(app->help));
    power_journal_tick(app);
    power_journal_refresh(app);
    view_dispatcher_switch_to_view(app->dispatcher, PowerViewMenu);
    view_dispatcher_run(app->dispatcher);
    power_history_pause(&app->history);
    if(app->writable && app->history.count) power_store_save(app->storage, &app->history);
    view_dispatcher_remove_view(app->dispatcher, PowerViewMenu);
    view_dispatcher_remove_view(app->dispatcher, PowerViewDisplay);
    view_dispatcher_remove_view(app->dispatcher, PowerViewHelp);
    view_free(app->view);
    submenu_free(app->menu);
    text_box_free(app->help);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_POWER);
    free(app);
    return 0;
}
