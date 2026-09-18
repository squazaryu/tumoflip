#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <storage/storage.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <toolbox/path.h>
#include <string.h>
#include "arf_file_probe.h"
#include "arf_expected_packs.h"

#define ARF_STATUS_STOP (1U << 0)

#define TAG "ArfStatus"

#define ARF_FULL_PATH    EXT_PATH("apps/ARF Tools/arf_subghz_full.fap")
#define ARF_MODULES_PATH EXT_PATH("apps_data/arf_subghz_full/modules/")

typedef enum {
    ArfToolsViewMain,
    ArfToolsViewText,
} ArfToolsView;

typedef enum {
    ArfToolsMenuAssets,
    ArfToolsMenuPacks,
    ArfToolsMenuCapabilities,
    ArfToolsMenuAbout,
} ArfToolsMenu;

typedef enum {
    ArfToolsEventAssets,
    ArfToolsEventPacks,
    ArfToolsEventCapabilities,
    ArfToolsEventAbout,
} ArfToolsEvent;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    TextBox* text_box;
    FuriString* text;
    FuriString* scan_text;
    FuriThread* scan_worker;
    bool scan_packs;
    bool scan_cancelled;
    bool showing_text;
} ArfToolsApp;

static void arf_tools_set_text(ArfToolsApp* app, const char* text) {
    furi_string_set_str(app->text, text);
    text_box_reset(app->text_box);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    app->showing_text = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewText);
}

static void arf_tools_set_text_string(ArfToolsApp* app) {
    text_box_reset(app->text_box);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    app->showing_text = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewText);
}

static bool arf_tools_scan_cancelled(void) {
    return (furi_thread_flags_get() & ARF_STATUS_STOP) != 0;
}

static void arf_tools_probe(ArfToolsApp* app, const char* label, const char* path) {
    if(arf_tools_scan_cancelled()) return;
    ArfFileProbe probe;
    arf_file_probe(app->storage, path, &probe);
    furi_string_cat_printf(app->scan_text, "%s\n%s\n", label, arf_file_status_text(probe.status));
    if(probe.status == ArfFileHeaderCompatible) {
        furi_string_cat_printf(
            app->scan_text,
            "API %u.%u; F%u; v%lu.%lu\n%lu bytes\n",
            probe.api_major,
            probe.api_minor,
            probe.target,
            (unsigned long)(probe.app_version >> 16),
            (unsigned long)(probe.app_version & 0xffff),
            (unsigned long)probe.bytes);
    }
    furi_string_cat_str(app->scan_text, "\n");
}

static void arf_tools_scan_modules(ArfToolsApp* app) {
    static const struct {
        const char* label;
        const char* path;
    } modules[] = {
        {"Full launcher", ARF_FULL_PATH},
        {"Status", ARF_MODULES_PATH "arf_status.fap"},
        {"ProtoPirate", ARF_MODULES_PATH "proto_pirate.fap"},
        {"KeeLoq", ARF_MODULES_PATH "arf_keeloq.fap"},
        {"Counter BF", ARF_MODULES_PATH "arf_counter_bf.fap"},
        {"Car Emulate", ARF_MODULES_PATH "arf_car_emulate.fap"},
        {"PSA Decrypt", ARF_MODULES_PATH "arf_psa_decrypt.fap"},
        {"RollJam", ARF_MODULES_PATH "rolljam.fap"},
        {"SubBrute", ARF_MODULES_PATH "subghz_bruteforcer.fap"},
        {"Capture converter",
         EXT_PATH("apps_data/arf_subghz_full/packages/protopirate_to_subghz.fap")},
        {"Capture Inspector",
         EXT_PATH("apps_data/arf_subghz_full/packages/capture_inspector.fap")},
    };
    for(size_t i = 0; i < COUNT_OF(modules) && !arf_tools_scan_cancelled(); i++)
        arf_tools_probe(app, modules[i].label, modules[i].path);
}

static void arf_tools_scan_packs(ArfToolsApp* app) {
    FuriString* path = furi_string_alloc();
    unsigned examined = 0;
    for(size_t i = 0; i < COUNT_OF(arf_expected_packs) && !arf_tools_scan_cancelled(); i++) {
        furi_string_printf(path, EXT_PATH("apps_data/subghz/plugins/%s"), arf_expected_packs[i]);
        arf_tools_probe(app, arf_expected_packs[i], furi_string_get_cstr(path));
        examined++;
    }
    furi_string_cat_printf(
        app->scan_text,
        "Managed files examined: %u/%u\nExtra custom files are not checked.\n",
        examined,
        (unsigned)COUNT_OF(arf_expected_packs));
    furi_string_free(path);
}

static int32_t arf_tools_scan_worker(void* context) {
    ArfToolsApp* app = context;
    furi_string_set(
        app->scan_text,
        "Manifest/API check only\nHash: not verified\nNo trusted hash reference.\n"
        "Imports/run: not tested.\nHardware: not tested.\n\n");
    if(app->scan_packs)
        arf_tools_scan_packs(app);
    else
        arf_tools_scan_modules(app);
    app->scan_cancelled = arf_tools_scan_cancelled();
    if(app->scan_cancelled) furi_string_cat_str(app->scan_text, "Cancelled; incomplete scan.\n");
    return 0;
}

static void arf_tools_start_scan(ArfToolsApp* app, bool packs) {
    if(app->scan_worker) return;
    app->scan_packs = packs;
    app->scan_cancelled = false;
    arf_tools_set_text(app, "Reading metadata...\nNo apps are executed.\nBack: cancel");
    app->scan_worker = furi_thread_alloc_ex("ArfMetadata", 3072, arf_tools_scan_worker, app);
    furi_thread_set_priority(app->scan_worker, FuriThreadPriorityLow);
    furi_thread_start(app->scan_worker);
}

static void arf_tools_tick(void* context) {
    ArfToolsApp* app = context;
    if(!app->scan_worker || furi_thread_get_state(app->scan_worker) != FuriThreadStateStopped)
        return;
    furi_thread_join(app->scan_worker);
    furi_thread_free(app->scan_worker);
    app->scan_worker = NULL;
    arf_tools_set_text(app, furi_string_get_cstr(app->scan_text));
}

static void arf_tools_append_protocol_capability(ArfToolsApp* app, const char* name) {
    const SubGhzProtocol* protocol =
        subghz_protocol_registry_get_by_name(&subghz_protocol_registry, name);
    if(!protocol) {
        furi_string_cat_printf(app->text, "%s: missing\n", name);
        return;
    }

    const SubGhzRadioBrokerProtocolCapability capability =
        subghz_radio_broker_protocol_capability(protocol);
    furi_string_cat_printf(
        app->text,
        "%s: %s%s%s %s%s %s%s\n",
        name,
        capability.bands & SubGhzRadioBrokerBand315 ? "315 " : "",
        capability.bands & SubGhzRadioBrokerBand433 ? "433 " : "",
        capability.bands & SubGhzRadioBrokerBand868 ? "868 " : "",
        capability.presets & SubGhzRadioBrokerPresetOok650 ? "AM " : "",
        capability.presets & SubGhzRadioBrokerPresetFsk238 ? "FM " : "",
        capability.directions & SubGhzRadioBrokerDirectionReceive ? "RX " : "",
        capability.directions & SubGhzRadioBrokerDirectionTransmit ? "TX" : "");
}

static void arf_tools_show_capabilities(ArfToolsApp* app) {
    size_t receive_count = 0;
    size_t transmit_count = 0;
    size_t am_count = 0;
    size_t fm_count = 0;
    const size_t protocol_count = subghz_protocol_registry_count(&subghz_protocol_registry);
    for(size_t i = 0; i < protocol_count; i++) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(&subghz_protocol_registry, i);
        const SubGhzRadioBrokerProtocolCapability capability =
            subghz_radio_broker_protocol_capability(protocol);
        if(capability.directions & SubGhzRadioBrokerDirectionReceive) receive_count++;
        if(capability.directions & SubGhzRadioBrokerDirectionTransmit) transmit_count++;
        if(capability.presets & SubGhzRadioBrokerPresetOok650) am_count++;
        if(capability.presets & SubGhzRadioBrokerPresetFsk238) fm_count++;
    }

    furi_string_printf(
        app->text,
        "RF Capabilities v%u\n\n"
        "Core registry: %u\n"
        "RX: %u  TX: %u\n"
        "AM: %u  FM: %u\n\n",
        SUBGHZ_RADIO_BROKER_CAPABILITY_SCHEMA_VERSION,
        (unsigned)protocol_count,
        (unsigned)receive_count,
        (unsigned)transmit_count,
        (unsigned)am_count,
        (unsigned)fm_count);
    arf_tools_append_protocol_capability(app, "Holtek_HT12X");
    arf_tools_append_protocol_capability(app, "Linear");
    arf_tools_append_protocol_capability(app, "Cham_Code");
    furi_string_cat_str(
        app->text,
        "\nCore registry only. Installed packs are not activated by this check.\nNot a transmission permit.");
    arf_tools_set_text_string(app);
}

static void arf_tools_show_about(ArfToolsApp* app) {
    arf_tools_set_text(
        app,
        "ARF Status 0.4\n\n"
        "Reads ELF metadata without executing apps or unpacking their assets.\n\n"
        "Header compatibility is not proof of integrity, resolved imports or hardware acceptance.\n\n"
        "Frequency Analyzer is provided by the core Sub-GHz app.");
}

static void arf_tools_menu_callback(void* context, uint32_t index) {
    ArfToolsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool arf_tools_custom_event_callback(void* context, uint32_t event) {
    ArfToolsApp* app = context;

    if(app->scan_worker) return true;
    switch(event) {
    case ArfToolsEventAssets:
        arf_tools_start_scan(app, false);
        return true;
    case ArfToolsEventPacks:
        arf_tools_start_scan(app, true);
        return true;
    case ArfToolsEventCapabilities:
        arf_tools_show_capabilities(app);
        return true;
    case ArfToolsEventAbout:
        arf_tools_show_about(app);
        return true;
    default:
        return false;
    }
}

static bool arf_tools_back(void* context) {
    ArfToolsApp* app = context;
    if(app->scan_worker) {
        if(furi_thread_get_state(app->scan_worker) != FuriThreadStateStopped)
            furi_thread_flags_set(furi_thread_get_id(app->scan_worker), ARF_STATUS_STOP);
        arf_tools_set_text(app, "Cancelling metadata check...");
        return true;
    }
    if(!app->showing_text) return false;
    app->showing_text = false;
    view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewMain);
    return true;
}

static ArfToolsApp* arf_tools_app_alloc(void) {
    ArfToolsApp* app = calloc(1, sizeof(ArfToolsApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->text = furi_string_alloc();
    app->scan_text = furi_string_alloc();

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, arf_tools_back);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, arf_tools_tick, 100);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, arf_tools_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "ARF Status");
    submenu_add_item(
        app->main_menu, "Module metadata", ArfToolsMenuAssets, arf_tools_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Protocol Pack files", ArfToolsMenuPacks, arf_tools_menu_callback, app);
    submenu_add_item(
        app->main_menu, "RF Capabilities", ArfToolsMenuCapabilities, arf_tools_menu_callback, app);
    submenu_add_item(app->main_menu, "About", ArfToolsMenuAbout, arf_tools_menu_callback, app);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfToolsViewMain, submenu_get_view(app->main_menu));

    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_dispatcher_add_view(
        app->view_dispatcher, ArfToolsViewText, text_box_get_view(app->text_box));

    view_dispatcher_switch_to_view(app->view_dispatcher, ArfToolsViewMain);
    return app;
}

static void arf_tools_app_free(ArfToolsApp* app) {
    furi_assert(app);
    if(app->scan_worker) {
        if(furi_thread_get_state(app->scan_worker) != FuriThreadStateStopped)
            furi_thread_flags_set(furi_thread_get_id(app->scan_worker), ARF_STATUS_STOP);
        furi_thread_join(app->scan_worker);
        furi_thread_free(app->scan_worker);
    }

    view_dispatcher_remove_view(app->view_dispatcher, ArfToolsViewText);
    view_dispatcher_remove_view(app->view_dispatcher, ArfToolsViewMain);
    text_box_free(app->text_box);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    furi_string_free(app->text);
    furi_string_free(app->scan_text);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t arf_tools_main(void* p) {
    UNUSED(p);
    ArfToolsApp* app = arf_tools_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    arf_tools_app_free(app);
    return 0;
}
