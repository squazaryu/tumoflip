#include "sensor_cards.h"
#include "sensor_runtime.h"
#include "field_report.h"
#include <dialogs/dialogs.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <furi_hal_rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {SensorMenu, SensorDisplay, SensorHelp};
#define SENSOR_STORE_MAGIC 0x53454E31U
typedef struct {
    SensorCard card;
    uint32_t now;
    uint32_t lost;
    uint8_t index;
    uint8_t count;
    bool listening;
    bool graph;
    bool writable;
    bool saved;
} SensorViewModel;
typedef struct {
    SensorCards cards;
    SensorRuntime runtime;
    Storage* storage;
    DialogsApp* dialogs;
    Gui* gui;
    ViewDispatcher* dispatcher;
    View* view;
    Submenu* menu;
    TextBox* help;
    char object[32];
    char root[160];
    uint32_t frequency;
    uint8_t selected;
    bool graph;
    bool writable;
    bool saved;
} SensorApp;

static void sensor_app_refresh(SensorApp* app) {
    with_view_model(app->view, SensorViewModel * model, {
        model->count = app->cards.count;
        model->index = app->selected;
        if(app->selected < app->cards.count) model->card = app->cards.cards[app->selected];
        model->now = furi_hal_rtc_get_timestamp();
        model->listening = app->runtime.listening;
        model->lost = app->runtime.lost;
        model->graph = app->graph;
        model->writable = app->writable;
        model->saved = app->saved;
    }, true);
}

static void sensor_app_draw(Canvas* canvas, void* context) {
    const SensorViewModel* model = context;
    char text[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 8, "Sensor Watch");
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, model->listening ? "RX" : "STOPPED");
    canvas_draw_line(canvas, 0, 10, 127, 10);
    if(!model->count) {
        canvas_draw_str(canvas, 1, 25, "No decoded sensors yet");
        canvas_draw_str(canvas, 1, 37, "Menu: start RX / frequency");
        canvas_draw_str(canvas, 1, 49, "Internal CC1101 / AM650");
    } else {
        const SensorPoint* point = sensor_card_point(&model->card, model->card.count - 1U);
        snprintf(text, sizeof(text), "%.31s", model->card.protocol);
        size_t title_length = strlen(text);
        while(title_length && canvas_string_width(canvas, text) > 126U) text[--title_length] = 0;
        canvas_draw_str(canvas, 1, 20, text);
        if(model->graph) {
            int minimum = 1000, maximum = -800;
            for(unsigned i = 0; i < model->card.count; i++) {
                const SensorPoint* item = sensor_card_point(&model->card, i);
                if(!(item->fields & SensorTemperature)) continue;
                if(item->temperature < minimum) minimum = item->temperature;
                if(item->temperature > maximum) maximum = item->temperature;
            }
            if(minimum > maximum) {
                canvas_draw_str(canvas, 1, 35, "No temperature values");
            } else {
                const unsigned range = maximum > minimum ? (unsigned)(maximum - minimum) : 1U;
                for(unsigned i = 0; i < model->card.count; i++) {
                    const SensorPoint* item = sensor_card_point(&model->card, i);
                    if(item->fields & SensorTemperature) {
                        const unsigned y = 43U - (item->temperature - minimum) * 15U / range;
                        canvas_draw_dot(canvas, 1U + i * 4U, y);
                    }
                }
            }
            if(minimum <= maximum) snprintf(text, sizeof(text), "%s%d.%d..%s%d.%dC /%u",
                minimum < 0 ? "-" : "", abs(minimum) / 10, abs(minimum) % 10,
                maximum < 0 ? "-" : "", abs(maximum) / 10, abs(maximum) % 10, model->card.count);
            else snprintf(text, sizeof(text), "%u samples / no temp", model->card.count);
            canvas_draw_str(canvas, 1, 51, text);
        } else {
            canvas_set_font(canvas, FontPrimary);
            if(point->fields & SensorTemperature) snprintf(text, sizeof(text), "%s%d.%d C",
                point->temperature < 0 ? "-" : "", abs(point->temperature) / 10, abs(point->temperature) % 10);
            else snprintf(text, sizeof(text), "-- C");
            canvas_draw_str(canvas, 1, 31, text);
            if(point->fields & SensorHumidity) snprintf(text, sizeof(text), "%u%%", point->humidity);
            else snprintf(text, sizeof(text), "--%%");
            canvas_draw_str_aligned(canvas, 127, 31, AlignRight, AlignBottom, text);
            canvas_set_font(canvas, FontSecondary);
            snprintf(text, sizeof(text), "%08lX C:%s", (unsigned long)model->card.id,
                     model->card.channel == 255U ? "?" : "");
            if(model->card.channel != 255U) snprintf(text, sizeof(text), "%08lX C:%u",
                                                   (unsigned long)model->card.id, model->card.channel);
            canvas_draw_str(canvas, 1, 41, text);
            snprintf(text, sizeof(text), "B:%s", !(point->fields & SensorBattery) ? "?" : point->battery_low ? "LOW" : "OK");
            canvas_draw_str_aligned(canvas, 127, 41, AlignRight, AlignBottom, text);
            if(model->now < point->timestamp) snprintf(text, sizeof(text), "Clock changed");
            else snprintf(text, sizeof(text), "%lu s ago", (unsigned long)(model->now - point->timestamp));
            canvas_draw_str(canvas, 1, 51, text);
            snprintf(text, sizeof(text), "%u/%u %s", model->index + 1U, model->count,
                     !model->writable ? "RO" : model->saved ? "SD" : "RAM");
            canvas_draw_str_aligned(canvas, 127, 51, AlignRight, AlignBottom, text);
        }
    }
    elements_button_left(canvas, "Prev");
    elements_button_center(canvas, "Menu");
    elements_button_right(canvas, "Next");
}

static bool sensor_app_save(SensorApp* app) {
    if(!app->writable || !sensor_cards_valid(&app->cards)) return false;
    if(!storage_simply_mkdir(app->storage, EXT_PATH("apps_data")) ||
       !storage_simply_mkdir(app->storage, LIBRARY_ROOT) ||
       !storage_simply_mkdir(app->storage, LIBRARY_ROOT "/sensors")) return false;
    app->saved = field_record_save(app->storage, app->root, SENSOR_STORE_MAGIC,
                                   &app->cards, sizeof(app->cards));
    return app->saved;
}

static void sensor_app_stop(SensorApp* app) {
    sensor_runtime_stop(&app->runtime);
    if(app->cards.count) sensor_app_save(app);
    sensor_app_refresh(app);
}

static uint32_t sensor_app_previous(void* context) {
    SensorApp* app = context;
    sensor_app_stop(app);
    return SensorMenu;
}

static uint32_t sensor_app_exit(void* context) {
    sensor_app_stop(context);
    return VIEW_NONE;
}

static void sensor_app_message(SensorApp* app, const char* header, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 3, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
}

static bool sensor_app_row(void* context, size_t index, char* row, size_t capacity) {
    SensorApp* app = context;
    for(unsigned i = 0; i < app->cards.count; i++) {
        const SensorCard* card = &app->cards.cards[i];
        if(index < card->count) {
            const int prefix = snprintf(row, capacity, "object:%s,", app->object);
            return prefix > 0 && (size_t)prefix < capacity &&
                   sensor_card_row(card, index, row + prefix, capacity - prefix);
        }
        index -= card->count;
    }
    return false;
}

static void sensor_app_action(void* context, uint32_t index) {
    SensorApp* app = context;
    if(index == 0U) {
        if(!sensor_runtime_start(&app->runtime, &app->cards, app->frequency))
            sensor_app_message(app, "RX not started", "Radio busy / low heap\nor unavailable CC1101.\nNo TX was requested.");
        view_dispatcher_switch_to_view(app->dispatcher, SensorDisplay);
    } else if(index == 1U) sensor_app_stop(app);
    else if(index == 2U || index == 3U) {
        app->graph = index == 3U;
        view_dispatcher_switch_to_view(app->dispatcher, SensorDisplay);
    } else if(index >= 16U && index < 19U) {
        sensor_app_stop(app);
        const uint32_t frequencies[] = {315000000U, 433920000U, 868350000U};
        app->frequency = frequencies[index - 16U];
    } else if(index == 4U) {
        sensor_app_stop(app);
        size_t count = 0;
        for(unsigned i = 0; i < app->cards.count; i++) count += app->cards.cards[i].count;
        char path[256];
        const bool ok = field_report_save(app->storage, "sensors", furi_hal_rtc_get_timestamp(),
            "object,protocol,id,channel,frequency_hz,timestamp,temp_tenths,humidity,battery_low,frames\n",
            count, sensor_app_row, app, path, sizeof(path));
        sensor_app_message(app, ok ? "CSV verified" : "Export failed", ok ?
            "Device Library / reports\nNew file with object ID.\nReadback + SHA256 OK." : "No samples or SD\nwrite/readback failed.");
    } else {
        sensor_app_stop(app);
        view_dispatcher_switch_to_view(app->dispatcher, SensorHelp);
    }
    sensor_app_refresh(app);
}

static bool sensor_app_input(InputEvent* event, void* context) {
    SensorApp* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk) {
        sensor_app_stop(app);
        view_dispatcher_switch_to_view(app->dispatcher, SensorMenu);
    } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        if(app->cards.count) app->selected = (app->selected +
            (event->key == InputKeyRight ? 1U : app->cards.count - 1U)) % app->cards.count;
    } else return false;
    sensor_app_refresh(app);
    return true;
}

static void sensor_app_tick(void* context) {
    SensorApp* app = context;
    if(sensor_runtime_tick(&app->runtime)) app->saved = false;
    sensor_app_refresh(app);
}

int32_t sensor_watch_app(void* context) {
    const char* argument = context;
    if(argument && argument[0] && (strncmp(argument, "object:", 7) || !library_name_valid(argument + 7))) return -1;
    SensorApp* app = calloc(1, sizeof(*app));
    if(!app) return -1;
    snprintf(app->object, sizeof(app->object), "%s", argument && argument[0] ? argument + 7 : "Global");
    snprintf(app->root, sizeof(app->root), LIBRARY_ROOT "/sensors/%s%s",
             argument && argument[0] ? "obj_" : "", argument && argument[0] ? app->object : "global");
    app->frequency = 433920000U;
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->gui = furi_record_open(RECORD_GUI);
    sensor_cards_init(&app->cards);
    app->writable = true;
    if(field_record_load(app->storage, app->root, SENSOR_STORE_MAGIC, &app->cards, sizeof(app->cards)) &&
       sensor_cards_valid(&app->cards)) app->saved = true;
    else {
        sensor_cards_init(&app->cards);
        for(unsigned i = 0; i < 4U; i++) {
            char path[192];
            snprintf(path, sizeof(path), "%s/%u.record", app->root, i);
            if(storage_common_stat(app->storage, path, NULL) != FSE_NOT_EXIST) app->writable = false;
        }
    }
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->help = text_box_alloc();
    app->view = view_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->dispatcher, sensor_app_tick, 50);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(SensorViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, sensor_app_draw);
    view_set_input_callback(app->view, sensor_app_input);
    view_set_previous_callback(app->view, sensor_app_previous);
    view_set_previous_callback(submenu_get_view(app->menu), sensor_app_exit);
    view_set_previous_callback(text_box_get_view(app->help), sensor_app_previous);
    submenu_set_header(app->menu, "Sensor Watch / AM650");
    const char* labels[] = {"Start receiver", "Stop / save history", "Sensor cards", "Temperature history", "Export checked CSV", "Scope / limits"};
    for(unsigned i = 0; i < COUNT_OF(labels); i++) submenu_add_item(app->menu, labels[i], i, sensor_app_action, app);
    const char* bands[] = {"315.00 MHz", "433.92 MHz (default)", "868.35 MHz"};
    for(unsigned i = 0; i < COUNT_OF(bands); i++) submenu_add_item(app->menu, bands[i], 16U + i, sensor_app_action, app);
    text_box_set_text(app->help,
        "Sensor Watch 1.0\n\n25 local Weather Editor\nRX decoders. Internal\nCC1101, AM650 only.\nNo TX or auto-start.\n\n"
        "Identity = decoded\nprotocol + ID + channel\n+ frequency. Not proof\nof authenticity; IDs\ncan change with battery.\n\n"
        "8 cards, 32 samples each.\nFull table never evicts.\nAge uses Flipper clock.\nRepeated frames in one\nsecond are coalesced.\n\n"
        "Stop/Back saves checked\nSD slots. Bad storage\nopens read-only, never\nerases user captures.\nHistory is app-only.\nNo background guarantee.\n\n"
        "Device Library opens\nper-object histories.\nCSV in reports includes\nobject ID and units.\nCopy via SD/USB reader.\n\n"
        "Temperature graph is\nauto-scaled to observed\nmin/max, not a fixed\nphysical Y scale.\nX is sample order, not\nelapsed time spacing.");
    view_dispatcher_add_view(app->dispatcher, SensorMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, SensorDisplay, app->view);
    view_dispatcher_add_view(app->dispatcher, SensorHelp, text_box_get_view(app->help));
    sensor_app_refresh(app);
    view_dispatcher_switch_to_view(app->dispatcher, SensorMenu);
    view_dispatcher_run(app->dispatcher);
    sensor_app_stop(app);
    for(unsigned i = 0; i < 3U; i++) view_dispatcher_remove_view(app->dispatcher, i);
    view_free(app->view);
    submenu_free(app->menu);
    text_box_free(app->help);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
