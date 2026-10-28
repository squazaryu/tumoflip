#include "varta_inspect.h"
#include "field_report.h"
#include <dialogs/dialogs.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <furi_hal.h>
#include <lib/lfrfid/lfrfid_worker.h>
#include <lib/flipper_format/flipper_format_i.h>
#include <toolbox/stream/file_stream.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {VartaMenu, VartaDisplay, VartaText, VartaReadEvent = 100};
typedef struct {VartaInspection result; uint8_t seconds; bool reading; bool ready;} VartaViewModel;
typedef struct {
    VartaInspection result;
    ProtocolDict* dict;
    LFRFIDWorker* worker;
    SubGhzRadioBroker* broker;
    SubGhzRadioBrokerLease lease;
    Storage* storage;
    DialogsApp* dialogs;
    Gui* gui;
    ViewDispatcher* dispatcher;
    View* view;
    Submenu* menu;
    TextBox* text;
    FuriString* detail;
    FuriString* path;
    uint32_t started;
    const char* source;
    bool reading;
    bool ready;
} VartaApp;

static void varta_app_refresh(VartaApp* app) {
    with_view_model(app->view, VartaViewModel * model, {
        model->result = app->result;
        model->reading = app->reading;
        model->ready = app->ready;
        const uint32_t elapsed = (furi_get_tick() - app->started) / 1000U;
        model->seconds = elapsed < 15U ? 15U - elapsed : 0U;
    }, true);
}

static void varta_app_draw(Canvas* canvas, void* context) {
    const VartaViewModel* model = context;
    char text[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 8, "Varta Inspector");
    canvas_draw_line(canvas, 0, 10, 127, 10);
    if(model->reading) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignBottom, "Present your EM4100");
        canvas_set_font(canvas, FontSecondary);
        snprintf(text, sizeof(text), "125 kHz ASK / %u s", model->seconds);
        canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignBottom, text);
        elements_button_center(canvas, "Stop");
    } else if(model->ready) {
        const VartaInspection* result = &model->result;
        snprintf(text, sizeof(text), "%02X%02X%02X%02X%02X", result->id[0], result->id[1],
                 result->id[2], result->id[3], result->id[4]);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignBottom, text);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignBottom,
                               result->known_prefix ? "Varta-like ID prefix" : "Unusual ID prefix");
        canvas_draw_str_aligned(canvas, 64, 49, AlignCenter, AlignBottom, "Not proof of authenticity");
        elements_button_center(canvas, "Menu");
    } else {
        canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignBottom, "No EM4100 decoded");
        canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignBottom, "Read again / inspect a file");
        elements_button_center(canvas, "Menu");
    }
}

static void varta_app_stop(VartaApp* app) {
    if(app->reading) {
        lfrfid_worker_stop(app->worker);
        lfrfid_worker_stop_thread(app->worker);
        app->reading = false;
    }
    if(app->broker) {
        subghz_radio_broker_release(app->broker, &app->lease);
        furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
        app->broker = NULL;
    }
}

static uint32_t varta_app_previous(void* context) {
    VartaApp* app = context;
    varta_app_stop(app);
    return VartaMenu;
}

static uint32_t varta_app_exit(void* context) {
    varta_app_stop(context);
    return VIEW_NONE;
}

static void varta_app_message(VartaApp* app, const char* header, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 3, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
}

static void varta_app_read_done(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    VartaApp* app = context;
    if(result == LFRFIDWorkerReadDone && protocol >= 0 && protocol < 3)
        view_dispatcher_send_custom_event(app->dispatcher, VartaReadEvent + protocol);
}

static bool varta_app_event(void* context, uint32_t event) {
    VartaApp* app = context;
    if(event < VartaReadEvent || event >= VartaReadEvent + 3U || !app->reading) return false;
    varta_app_stop(app); // joins before accessing the dictionary or releasing the coil
    const unsigned protocol = event - VartaReadEvent;
    uint8_t id[5];
    if(protocol_dict_get_data_size(app->dict, protocol) != sizeof(id)) return false;
    protocol_dict_get_data(app->dict, protocol, id, sizeof(id));
    const uint8_t clocks[] = {64, 32, 16};
    varta_inspect_id(&app->result, id, clocks[protocol]);
    app->source = "live";
    app->ready = true;
    varta_app_refresh(app);
    return true;
}

static bool varta_app_load_dump(VartaApp* app) {
    FlipperFormat* file = flipper_format_file_alloc(app->storage);
    FuriString* text = furi_string_alloc();
    uint32_t version = 0;
    bool opened = file_stream_open(flipper_format_get_raw_stream(file),
        furi_string_get_cstr(app->path), FSAM_READ, FSOM_OPEN_EXISTING);
    bool ok = opened && flipper_format_read_header(file, text, &version) && version == 1U &&
              !furi_string_cmp_str(text, "Flipper T5577 dump");
    uint8_t mask = 0;
    uint32_t blocks[8] = {0};
    if(ok) ok = flipper_format_read_string(file, "Page 0 blocks read", text) &&
                varta_inspect_read_list(furi_string_get_cstr(text), &mask);
    for(unsigned i = 0; ok && i < 8U; i++) {
        if(!(mask & (1U << i))) continue;
        char key[24];
        uint8_t bytes[4];
        snprintf(key, sizeof(key), "Page 0 Block %u", i);
        if(!flipper_format_rewind(file) || !flipper_format_read_hex(file, key, bytes, sizeof(bytes))) {
            ok = false;
            break;
        }
        for(unsigned j = 0; j < sizeof(bytes); j++) blocks[i] = (blocks[i] << 8U) | bytes[j];
    }
    if(opened) ok = flipper_format_file_close(file) && ok;
    if(ok) varta_inspect_dump(&app->result, blocks, mask);
    furi_string_free(text);
    flipper_format_free(file);
    // Password fields are neither read nor included in reports.
    return ok;
}

static bool varta_app_load_rfid(VartaApp* app) {
    FlipperFormat* file = flipper_format_file_alloc(app->storage);
    FuriString* text = furi_string_alloc();
    const bool opened = file_stream_open(flipper_format_get_raw_stream(file),
        furi_string_get_cstr(app->path), FSAM_READ, FSOM_OPEN_EXISTING);
    uint32_t version = 0;
    bool ok = opened && flipper_format_read_header(file, text, &version) && version == 1U &&
              !furi_string_cmp_str(text, "Flipper RFID key") &&
              flipper_format_read_string(file, "Key type", text);
    unsigned protocol = 0;
    if(ok) {
        while(protocol < 3U && furi_string_cmp_str(text, protocol_dict_get_name(app->dict, protocol))) ++protocol;
        ok = protocol < 3U;
    }
    uint8_t id[5];
    uint32_t count = 0;
    if(ok) ok = flipper_format_get_value_count(file, "Data", &count) && count == sizeof(id) &&
                flipper_format_read_hex(file, "Data", id, sizeof(id));
    if(opened) ok = flipper_format_file_close(file) && ok;
    if(ok) {
        const uint8_t clocks[] = {64, 32, 16};
        varta_inspect_id(&app->result, id, clocks[protocol]);
    }
    furi_string_free(text);
    flipper_format_free(file);
    return ok;
}

static void varta_app_file(VartaApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, ".rfid|.t5577", NULL);
    options.base_path = EXT_PATH("lfrfid");
    if(!dialog_file_browser_show(app->dialogs, app->path, app->path, &options)) return;
    app->ready = false;
    FileInfo info;
    bool ok = storage_common_stat(app->storage, furi_string_get_cstr(app->path), &info) == FSE_OK &&
              !file_info_is_dir(&info) && info.size > 0 && info.size <= 4096U;
    if(ok && furi_string_end_withi(app->path, ".t5577")) {
        ok = varta_app_load_dump(app);
        app->source = "dump";
        if(ok && app->result.status == VartaMissingBlocks) {
            varta_app_message(app, "Missing ID blocks", "Blocks 1 and 2 must\nbe marked as read.\nPadding is not evidence.");
            return;
        }
        if(ok && app->result.status == VartaBadFrame) ok = false;
    } else if(ok) {
        ok = varta_app_load_rfid(app);
        app->source = "rfid";
    }
    app->ready = ok;
    if(ok) view_dispatcher_switch_to_view(app->dispatcher, VartaDisplay);
    else varta_app_message(app, "Cannot inspect", "Need a valid EM4100\nRFID / T5577 file,\nup to 4 KiB. No changes.");
}

static bool varta_app_row(void* context, size_t index, char* row, size_t capacity) {
    VartaApp* app = context;
    if(index || !app->ready) return false;
    const VartaInspection* r = &app->result;
    const int size = snprintf(row, capacity, "%s,%02X%02X%02X%02X%02X,%u,%u,%u,%u,%u,%u\n",
        app->source, r->id[0], r->id[1], r->id[2], r->id[3], r->id[4], r->clock,
        r->known_prefix, r->has_config, r->config_matches, r->has_marker, r->marker_matches);
    return size > 0 && (size_t)size < capacity;
}

static void varta_app_action(void* context, uint32_t action) {
    VartaApp* app = context;
    varta_app_stop(app);
    if(action == 0U) {
        app->ready = false;
        app->broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
        if(!subghz_radio_broker_acquire(app->broker, "varta_inspector", 100U, &app->lease)) {
            furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
            app->broker = NULL;
            varta_app_message(app, "Hardware busy", "Another radio / GPIO\nsession is active.\nNo tag commands sent.");
            return;
        }
        const bool busy = furi_hal_bus_is_enabled(FuriHalBusTIM1) || furi_hal_bus_is_enabled(FuriHalBusTIM2);
        if(busy) {
            varta_app_stop(app);
            varta_app_message(app, "LF timers busy", "Stop other GPIO / LF\noperations first.\nNothing was reconfigured.");
            return;
        }
        app->started = furi_get_tick();
        app->reading = true;
        lfrfid_worker_start_thread(app->worker);
        lfrfid_worker_read_start(app->worker, LFRFIDWorkerReadTypeASKOnly, varta_app_read_done, app);
        view_dispatcher_switch_to_view(app->dispatcher, VartaDisplay);
    } else if(action == 1U) varta_app_file(app);
    else if(action == 2U && app->ready) {
        const VartaInspection* r = &app->result;
        furi_string_printf(app->detail,
            "Source: %s\nID: %02X%02X%02X%02X%02X\nClock: RF/%u (0=unknown)\nPrefix: %s\n\n"
            "Config: %s\nMarker: %s\n\nFile observations only.\nA generated dump can\nmatch the same constants.\n"
            "No proof of genuine\nhardware or access rights.\nNo passwords in reports.", app->source,
            r->id[0], r->id[1], r->id[2], r->id[3], r->id[4], r->clock, r->known_prefix ? "Varta-like" : "unusual",
            !r->has_config ? "not observed" : r->config_matches ? "matches format" : "different",
            !r->has_marker ? "not observed" : r->marker_matches ? "matches format" : "different");
        text_box_set_text(app->text, furi_string_get_cstr(app->detail));
        view_dispatcher_switch_to_view(app->dispatcher, VartaText);
    } else if(action == 3U) {
        char path[256];
        const bool ok = app->ready && field_report_save(app->storage, "varta", furi_hal_rtc_get_timestamp(),
            "source,id,clock,known_prefix,config_present,config_match,marker_present,marker_match\n",
            1, varta_app_row, app, path, sizeof(path));
        varta_app_message(app, ok ? "CSV verified" : "No report saved", ok ?
            "Device Library / reports\nReadback + SHA256 OK.\nSource file unchanged." : "Read / inspect an ID first.\nCheck SD availability.");
    } else {
        text_box_set_text(app->text,
            "Varta Inspector 1.0\n\nOwn EM4100 tags/files.\n125kHz ASK read, 15s max.\nStop/Back joins worker\nand releases the coil.\n\n"
            "A Varta-like prefix is\nnot authentication.\nA dump may be generated.\nBlock read flags matter:\nzero padding is ignored.\n\n"
            "No protected reads,\npassword capture, marker\nemulation, reconstruction\nor tag writes here.\nSome protected-read\ncommands can act as\nwrites on blank tags.\n\n"
            "Full Community Varta\nremains a separate app.\nStandard RFID features\nare unchanged.\n\n"
            "Reports are new checked\nCSVs, no passwords, under\napps_data/device_library\n/reports. Copy via SD\nor USB reader.");
        view_dispatcher_switch_to_view(app->dispatcher, VartaText);
    }
    varta_app_refresh(app);
}

static bool varta_app_input(InputEvent* event, void* context) {
    VartaApp* app = context;
    if(event->type != InputTypeShort || event->key != InputKeyOk) return false;
    varta_app_stop(app);
    varta_app_refresh(app);
    view_dispatcher_switch_to_view(app->dispatcher, VartaMenu);
    return true;
}

static void varta_app_tick(void* context) {
    VartaApp* app = context;
    if(app->reading && furi_get_tick() - app->started >= 15000U) varta_app_stop(app);
    varta_app_refresh(app);
}

int32_t tumo_varta_app(void* context) {
    UNUSED(context);
    VartaApp* app = calloc(1, sizeof(*app));
    if(!app) return -1;
    const ProtocolBase* protocols[] = {lfrfid_protocols[LFRFIDProtocolEM4100],
        lfrfid_protocols[LFRFIDProtocolEM4100_32], lfrfid_protocols[LFRFIDProtocolEM4100_16]};
    app->dict = protocol_dict_alloc(protocols, COUNT_OF(protocols));
    app->worker = lfrfid_worker_alloc(app->dict);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->text = text_box_alloc();
    app->detail = furi_string_alloc();
    app->path = furi_string_alloc_set(EXT_PATH("lfrfid"));
    app->view = view_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, varta_app_event);
    view_dispatcher_set_tick_event_callback(app->dispatcher, varta_app_tick, 500);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(VartaViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, varta_app_draw);
    view_set_input_callback(app->view, varta_app_input);
    view_set_previous_callback(app->view, varta_app_previous);
    view_set_previous_callback(submenu_get_view(app->menu), varta_app_exit);
    view_set_previous_callback(text_box_get_view(app->text), varta_app_previous);
    submenu_set_header(app->menu, "Varta Inspector");
    const char* labels[] = {"Read own EM4100", "Inspect saved file", "Observation details", "Export checked report", "Scope / safety"};
    for(unsigned i = 0; i < COUNT_OF(labels); i++) submenu_add_item(app->menu, labels[i], i, varta_app_action, app);
    view_dispatcher_add_view(app->dispatcher, VartaMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, VartaDisplay, app->view);
    view_dispatcher_add_view(app->dispatcher, VartaText, text_box_get_view(app->text));
    varta_app_refresh(app);
    view_dispatcher_switch_to_view(app->dispatcher, VartaMenu);
    view_dispatcher_run(app->dispatcher);
    varta_app_stop(app);
    for(unsigned i = 0; i < 3U; i++) view_dispatcher_remove_view(app->dispatcher, i);
    view_free(app->view);
    submenu_free(app->menu);
    text_box_free(app->text);
    furi_string_free(app->detail);
    furi_string_free(app->path);
    view_dispatcher_free(app->dispatcher);
    lfrfid_worker_free(app->worker);
    protocol_dict_free(app->dict);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
