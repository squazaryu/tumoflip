#include "uhf_inventory.h"
#include "uhf_runtime.h"
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

enum {UhfMenu, UhfDisplay, UhfTags, UhfText};
typedef struct {
    uint32_t errors;
    uint32_t full;
    uint8_t count;
    uint8_t version[3];
    bool connected;
    bool identified;
    bool running;
    bool timeout;
} UhfViewModel;
typedef struct {
    UhfInventory inventory;
    UhfRuntime runtime;
    Storage* storage;
    DialogsApp* dialogs;
    Gui* gui;
    ViewDispatcher* dispatcher;
    View* view;
    Submenu* menu;
    Submenu* tags;
    TextBox* text;
    FuriString* detail;
} UhfApp;

static void uhf_app_refresh(UhfApp* app) {
    with_view_model(app->view, UhfViewModel * model, {
        model->errors = app->inventory.errors;
        model->full = app->inventory.full;
        model->count = app->inventory.count;
        memcpy(model->version, app->inventory.version, 3);
        model->connected = app->runtime.serial != NULL;
        model->identified = app->inventory.identified;
        model->running = app->runtime.running;
        model->timeout = app->runtime.timed_out;
    }, true);
}

static void uhf_app_draw(Canvas* canvas, void* context) {
    const UhfViewModel* model = context;
    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 8, "UHF Inventory");
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, model->running ? "RF ON" : "RF OFF");
    canvas_draw_line(canvas, 0, 10, 127, 10);
    snprintf(line, sizeof(line), "Tags: %u / 32", model->count);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 1, 24, line);
    canvas_set_font(canvas, FontSecondary);
    if(model->identified) snprintf(line, sizeof(line), "Reader: %u.%u.%u", model->version[0], model->version[1], model->version[2]);
    else snprintf(line, sizeof(line), "%s", model->connected ? "Waiting for UCM601..." : "Connect UCM601 first");
    canvas_draw_str(canvas, 1, 36, line);
    if(model->timeout) snprintf(line, sizeof(line), "Session stopped / timeout");
    else snprintf(line, sizeof(line), "Bad:%lu%s Full:%lu%s",
        (unsigned long)(model->errors > 9999U ? 9999U : model->errors), model->errors > 9999U ? "+" : "",
        (unsigned long)(model->full > 9999U ? 9999U : model->full), model->full > 9999U ? "+" : "");
    canvas_draw_str(canvas, 1, 48, line);
    elements_button_left(canvas, "Tags");
    elements_button_center(canvas, model->running ? "Stop" : "Start");
    elements_button_right(canvas, "Menu");
}

static uint32_t uhf_app_previous(void* context) {
    UhfApp* app = context;
    uhf_runtime_stop(&app->runtime);
    uhf_app_refresh(app);
    return UhfMenu;
}

static uint32_t uhf_app_exit(void* context) {
    UhfApp* app = context;
    uhf_runtime_disconnect(&app->runtime);
    return VIEW_NONE;
}

static void uhf_app_message(UhfApp* app, const char* header, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 3, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
}

static void uhf_app_tag(void* context, uint32_t index) {
    UhfApp* app = context;
    if(index >= app->inventory.count) return;
    const UhfInventoryTag* tag = &app->inventory.tags[index];
    char epc[97];
    if(!uhf_inventory_epc(tag, epc, sizeof(epc))) return;
    furi_string_printf(app->detail, "Observed EPC\n");
    for(size_t offset = 0; offset < tag->length * 2U; offset += 16U)
        furi_string_cat_printf(app->detail, "%.16s\n", epc + offset);
    furi_string_cat_printf(app->detail,
        "\nPC: %04X / Ant: %u\nHits: %lu\nFirst: %lu\nLast: %lu\nSignal raw: %lu\nFreq raw: %lu\n\n"
        "Raw reader metadata,\nnot calibrated dBm/Hz.\nEPC is not proof of\nphysical authenticity.",
        tag->pc, tag->antenna, (unsigned long)tag->seen, (unsigned long)tag->first,
        (unsigned long)tag->last, (unsigned long)tag->signal_raw, (unsigned long)tag->frequency_raw);
    text_box_set_text(app->text, furi_string_get_cstr(app->detail));
    view_dispatcher_switch_to_view(app->dispatcher, UhfText);
}

static void uhf_app_tags(UhfApp* app) {
    uhf_runtime_stop(&app->runtime);
    submenu_reset(app->tags);
    submenu_set_header(app->tags, "Observed EPCs");
    for(unsigned i = 0; i < app->inventory.count; i++) {
        char hex[97], label[32];
        uhf_inventory_epc(&app->inventory.tags[i], hex, sizeof(hex));
        snprintf(label, sizeof(label), "%u %.12s...", i + 1U, hex);
        submenu_add_item(app->tags, label, i, uhf_app_tag, app);
    }
    if(!app->inventory.count) submenu_add_item(app->tags, "No tags yet", UINT32_MAX, uhf_app_tag, app);
    view_dispatcher_switch_to_view(app->dispatcher, UhfTags);
}

static bool uhf_app_row(void* context, size_t index, char* line, size_t capacity) {
    UhfApp* app = context;
    if(index >= app->inventory.count) return false;
    const UhfInventoryTag* tag = &app->inventory.tags[index];
    char epc[97];
    if(!uhf_inventory_epc(tag, epc, sizeof(epc))) return false;
    const int size = snprintf(line, capacity, "%s,%u,%u,%lu,%lu,%lu,%lu,%lu\n", epc,
        tag->pc, tag->antenna, (unsigned long)tag->seen, (unsigned long)tag->first,
        (unsigned long)tag->last, (unsigned long)tag->signal_raw, (unsigned long)tag->frequency_raw);
    return size > 0 && (size_t)size < capacity;
}

static void uhf_app_action(void* context, uint32_t action) {
    UhfApp* app = context;
    if(action != 1U) uhf_runtime_stop(&app->runtime);
    if(action == 0U) {
        if(!app->runtime.serial && !uhf_runtime_connect(&app->runtime, &app->inventory))
            uhf_app_message(app, "UART busy", "No port was taken.\nCheck expansion / UART\nand external reader power.");
        view_dispatcher_switch_to_view(app->dispatcher, UhfDisplay);
    } else if(action == 1U) {
        if(app->runtime.running) uhf_runtime_stop(&app->runtime);
        else if(app->runtime.serial && app->inventory.identified) {
            DialogMessage* message = dialog_message_alloc();
            dialog_message_set_header(message, "Start RF interrogation?", 64, 3, AlignCenter, AlignTop);
            dialog_message_set_text(message, "UCM601 region / power\nmust already be legal.\nUp to 60 seconds.", 64, 32, AlignCenter, AlignCenter);
            dialog_message_set_buttons(message, "Back", NULL, "Start");
            const DialogMessageButton result = dialog_message_show(app->dialogs, message);
            dialog_message_free(message);
            uhf_runtime_tick(&app->runtime);
            if(result == DialogMessageButtonRight && !uhf_runtime_start(&app->runtime))
                uhf_app_message(app, "No fresh reader reply", "Reconnect / wait for\na valid version reply.\nRF was not started.");
        } else uhf_app_message(app, "Reader not identified", "Connect UCM601 and\nwait for its version.\nRF was not started.");
        view_dispatcher_switch_to_view(app->dispatcher, UhfDisplay);
    } else if(action == 2U) uhf_app_tags(app);
    else if(action == 3U) {
        char path[256];
        const bool ok = field_report_save(app->storage, "uhf", furi_hal_rtc_get_timestamp(),
            "epc,pc,antenna,hits,first,last,signal_raw,frequency_raw\n", app->inventory.count,
            uhf_app_row, app, path, sizeof(path));
        uhf_app_message(app, ok ? "CSV verified" : "Export failed", ok ?
            "Device Library / reports\nNew file, read back\nand SHA256 checked." : "No observed tags or\nSD write/readback failed.");
    } else if(action == 4U) {
        text_box_set_text(app->text,
            "UCM601 inventory only\n\nSeparate UHF reader\nrequired, not Module One\nor ESP32-C5 alone.\n\n"
            "UART 115200 8N1:\n13 TX to reader RX\n14 RX to reader TX\nCommon GND, 3.3V logic.\n"
            "External reader power.\nNo automatic 5V/reset.\n\nRF interrogation is\nactive, not passive.\nReader region/power\nsettings are unchanged.\n"
            "Start is explicit,\nBack/Stop ends session.\nMax 60s, timeout stops.\nIf reader unplugged,\nstop cannot be verified.\n\n"
            "32 unique EPCs maximum.\nBad checksum/length\nframes are discarded.\nNo lock/kill/tag writes.\nFull Community UHF app\nremains available.\n\n"
            "New checked CSV under\napps_data/device_library\n/reports. Copy via SD\nor USB reader.");
        view_dispatcher_switch_to_view(app->dispatcher, UhfText);
    } else uhf_runtime_disconnect(&app->runtime);
    uhf_app_refresh(app);
}

static bool uhf_app_input(InputEvent* event, void* context) {
    UhfApp* app = context;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk) uhf_app_action(app, 1);
    else if(event->key == InputKeyLeft) uhf_app_tags(app);
    else if(event->key == InputKeyRight) {
        uhf_runtime_stop(&app->runtime);
        view_dispatcher_switch_to_view(app->dispatcher, UhfMenu);
    } else return false;
    return true;
}

static void uhf_app_tick(void* context) {
    UhfApp* app = context;
    uhf_runtime_tick(&app->runtime);
    uhf_app_refresh(app);
}

int32_t tumo_uhf_inventory_app(void* context) {
    UNUSED(context);
    UhfApp* app = calloc(1, sizeof(*app));
    if(!app) return -1;
    uhf_inventory_init(&app->inventory);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->tags = submenu_alloc();
    app->text = text_box_alloc();
    app->detail = furi_string_alloc();
    app->view = view_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->dispatcher, uhf_app_tick, 50);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(UhfViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, uhf_app_draw);
    view_set_input_callback(app->view, uhf_app_input);
    view_set_previous_callback(app->view, uhf_app_previous);
    view_set_previous_callback(submenu_get_view(app->menu), uhf_app_exit);
    view_set_previous_callback(submenu_get_view(app->tags), uhf_app_previous);
    view_set_previous_callback(text_box_get_view(app->text), uhf_app_previous);
    submenu_set_header(app->menu, "Tumo UHF Inventory");
    const char* labels[] = {"Connect / status", "Start / stop RF", "Observed tags", "Export checked CSV", "Wiring / scope", "Disconnect UART"};
    for(unsigned i = 0; i < COUNT_OF(labels); i++) submenu_add_item(app->menu, labels[i], i, uhf_app_action, app);
    view_dispatcher_add_view(app->dispatcher, UhfMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, UhfDisplay, app->view);
    view_dispatcher_add_view(app->dispatcher, UhfTags, submenu_get_view(app->tags));
    view_dispatcher_add_view(app->dispatcher, UhfText, text_box_get_view(app->text));
    uhf_app_refresh(app);
    view_dispatcher_switch_to_view(app->dispatcher, UhfMenu);
    view_dispatcher_run(app->dispatcher);
    uhf_runtime_disconnect(&app->runtime);
    for(unsigned i = 0; i < 4U; i++) view_dispatcher_remove_view(app->dispatcher, i);
    view_free(app->view);
    submenu_free(app->menu);
    submenu_free(app->tags);
    text_box_free(app->text);
    furi_string_free(app->detail);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
