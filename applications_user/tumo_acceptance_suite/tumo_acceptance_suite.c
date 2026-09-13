#include <furi.h>
#include <furi/core/thread_list.h>
#include <furi_hal_info.h>
#include <furi_hal_infrared.h>
#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <furi_hal_serial_control.h>
#include <furi_hal_version.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <subghz_radio_broker/subghz_radio_broker.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TUMO_ACCEPTANCE_DATA_DIR EXT_PATH("apps_data/tumo_acceptance_suite")
#define TUMO_ACCEPTANCE_STORAGE_PROBE_PATH \
    EXT_PATH("apps_data/tumo_acceptance_suite/.storage_probe.tmp")
#define TUMO_ACCEPTANCE_PACKAGE_STATE_PATH EXT_PATH(".tumoflip/package-state.txt")
#define TUMO_ACCEPTANCE_REPORT_SCHEMA      "tumoflip.acceptance/1"
#define TUMO_ACCEPTANCE_SUITE_VERSION      "0.2.0"
#define TUMO_ACCEPTANCE_REPORT_PATH_SIZE   128U
#define TUMO_ACCEPTANCE_MAX_THREAD_LINES   12U
#define TUMO_ACCEPTANCE_DETAIL_SIZE        96U

typedef enum {
    TumoAcceptanceViewMenu,
    TumoAcceptanceViewText,
} TumoAcceptanceView;

typedef enum {
    TumoAcceptanceActionRun,
    TumoAcceptanceActionExport,
    TumoAcceptanceActionAbout,
} TumoAcceptanceAction;

typedef enum {
    TumoAcceptanceResultSkip,
    TumoAcceptanceResultPass,
    TumoAcceptanceResultFail,
} TumoAcceptanceResult;

typedef struct {
    const char* label;
    const char* path;
    bool required;
} TumoAcceptancePathCheck;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    FuriString* text;
    bool self_test_ran;
    TumoAcceptanceResult storage_result;
    char storage_detail[TUMO_ACCEPTANCE_DETAIL_SIZE];
} TumoAcceptanceApp;

static const TumoAcceptancePathCheck tumo_acceptance_paths[] = {
    {"Module One Cockpit", EXT_PATH("apps/Module One/Diagnostics/cockpit.fap"), true},
    {"Runtime Trace", EXT_PATH("apps/Module One/Diagnostics/runtime_trace_viewer.fap"), true},
    {"Field Logger", EXT_PATH("apps/Module One/Field/field_logger.fap"), true},
    {"TumoSpectrum", EXT_PATH("apps/Module One/Signals/signal_workbench.fap"), true},
    {"TumoSpectrum profile",
     EXT_PATH("apps_data/signal_workbench/profiles/demo_pulse_pair.tproto"),
     true},
    {"Sensor Logger",
     EXT_PATH("apps/Module One/Sensors BME280/module_one_sensor_logger.fap"),
     true},
    {"BLE GATT Lab", EXT_PATH("apps/Module One/BLE/ble_gatt_lab.fap"), true},
    {"App Bridge Terminal", EXT_PATH("apps/Module One/BLE/app_bridge_terminal.fap"), true},
    {"TumoFlow", EXT_PATH("apps/Module One/Automation/tumoflow.fap"), true},
    {"Macro Deck", EXT_PATH("apps/Module One/Macros/tumo_macro_deck.fap"), true},
    {"TumoScript", EXT_PATH("apps/Module One/Scripts/tumoscript.fap"), true},
    {"Tumo IR Lab", EXT_PATH("apps/Module One/IR Blaster/tumo_ir_lab.fap"), true},
    {"Tumo XRemote", EXT_PATH("apps/Module One/IR Blaster/tumoflip_xremote.fap"), true},
    {"WiFi Mapper", EXT_PATH("apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"), true},
    {"ARF Sub-GHz Full", EXT_PATH("apps/ARF Tools/arf_subghz_full.fap"), true},
    {"ARF Status", EXT_PATH("apps_data/arf_subghz_full/modules/arf_status.fap"), true},
};

static bool tumo_acceptance_path_exists(Storage* storage, const char* path) {
    return storage_common_stat(storage, path, NULL) == FSE_OK;
}

static bool tumo_acceptance_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static void tumo_acceptance_format_filename_timestamp(char* output, size_t output_size) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        output,
        output_size,
        "%04u%02u%02u_%02u%02u%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static void tumo_acceptance_append_iso_timestamp(FuriString* output) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    furi_string_cat_printf(
        output,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static void tumo_acceptance_append_result(
    FuriString* output,
    const char* name,
    bool pass,
    const char* detail) {
    furi_string_cat_printf(output, "[%s] %s", pass ? "PASS" : "FAIL", name);
    if(detail && detail[0]) {
        furi_string_cat_printf(output, " - %s", detail);
    }
    furi_string_cat(output, "\n");
}

static void tumo_acceptance_append_status(
    FuriString* output,
    const char* name,
    TumoAcceptanceResult result,
    const char* detail) {
    const char* status = "SKIP";
    if(result == TumoAcceptanceResultPass) status = "PASS";
    if(result == TumoAcceptanceResultFail) status = "FAIL";
    furi_string_cat_printf(output, "[%s] %s", status, name);
    if(detail && detail[0]) furi_string_cat_printf(output, " - %s", detail);
    furi_string_cat(output, "\n");
}

static void tumo_acceptance_append_info(FuriString* output, const char* name, const char* detail) {
    furi_string_cat_printf(output, "[INFO] %s", name);
    if(detail && detail[0]) {
        furi_string_cat_printf(output, " - %s", detail);
    }
    furi_string_cat(output, "\n");
}

static const char* tumo_acceptance_radio_device_name(SubGhzRadioBrokerDevice device) {
    switch(device) {
    case SubGhzRadioBrokerDeviceInternal:
        return "internal";
    case SubGhzRadioBrokerDeviceExternalCC1101:
        return "external_cc1101";
    case SubGhzRadioBrokerDeviceDual:
        return "dual";
    default:
        return "unknown";
    }
}

static const char* tumo_acceptance_radio_state_name(SubGhzRadioBrokerState state) {
    switch(state) {
    case SubGhzRadioBrokerStateIdle:
        return "idle";
    case SubGhzRadioBrokerStateAcquired:
        return "acquired";
    case SubGhzRadioBrokerStateProbing:
        return "probing";
    case SubGhzRadioBrokerStateInitialized:
        return "initialized";
    case SubGhzRadioBrokerStateRx:
        return "rx";
    case SubGhzRadioBrokerStateTx:
        return "tx";
    case SubGhzRadioBrokerStateAsyncRx:
        return "async_rx";
    case SubGhzRadioBrokerStateAsyncTx:
        return "async_tx";
    case SubGhzRadioBrokerStateCleaningUp:
        return "cleaning_up";
    case SubGhzRadioBrokerStateExternalPowerOn:
        return "external_power_on";
    case SubGhzRadioBrokerStateReleasing:
        return "releasing";
    case SubGhzRadioBrokerStateError:
        return "error";
    default:
        return "unknown";
    }
}

static bool tumo_acceptance_thread_is_relevant(const FuriThreadListItem* item) {
    const char* name = item->name ? item->name : "";
    const char* app_id = item->app_id ? item->app_id : "";
    return item->stack_min_free < 512U || strstr(name, "nfc") || strstr(name, "Nfc") ||
           strstr(name, "subghz") || strstr(name, "SubGhz") || strstr(name, "radio") ||
           strstr(name, "Radio") || strstr(name, "loader") || strstr(name, "Loader") ||
           strstr(name, "cli") || strstr(name, "Cli") || strstr(app_id, "nfc") ||
           strstr(app_id, "subghz") || strstr(app_id, "radio");
}

static void tumo_acceptance_append_runtime_snapshot(
    TumoAcceptanceApp* app,
    FuriString* output,
    const char* stage) {
    furi_string_cat_printf(output, "\nRuntime snapshot (%s)\n", stage);
    furi_string_cat_printf(
        output,
        "Heap: total=%lu free=%lu min=%lu max_block=%lu\n",
        (unsigned long)memmgr_get_total_heap(),
        (unsigned long)memmgr_get_free_heap(),
        (unsigned long)memmgr_get_minimum_free_heap(),
        (unsigned long)memmgr_heap_get_max_free_block());

    FuriThreadList* thread_list = furi_thread_list_alloc();
    if(furi_thread_enumerate(thread_list)) {
        size_t relevant_count = 0;
        size_t printed_count = 0;
        uint32_t minimum_stack_free = UINT32_MAX;
        for(size_t i = 0; i < furi_thread_list_size(thread_list); i++) {
            const FuriThreadListItem* item = furi_thread_list_get_at(thread_list, i);
            if(item->stack_min_free < minimum_stack_free) {
                minimum_stack_free = item->stack_min_free;
            }
            if(!tumo_acceptance_thread_is_relevant(item)) continue;
            relevant_count++;
            if(printed_count >= TUMO_ACCEPTANCE_MAX_THREAD_LINES) continue;
            furi_string_cat_printf(
                output,
                "Thread: %s app=%s state=%s stack_min=%lu heap=%lu\n",
                item->name ? item->name : "?",
                item->app_id ? item->app_id : "?",
                item->state ? item->state : "?",
                (unsigned long)item->stack_min_free,
                (unsigned long)item->heap);
            printed_count++;
        }
        furi_string_cat_printf(
            output,
            "Threads: total=%zu relevant=%zu min_stack_free=%lu%s\n",
            furi_thread_list_size(thread_list),
            relevant_count,
            (unsigned long)minimum_stack_free,
            relevant_count > printed_count ? " (truncated)" : "");
    } else {
        tumo_acceptance_append_info(output, "Thread enumeration", "unavailable");
    }
    furi_thread_list_free(thread_list);

    SubGhzRadioBroker* radio_broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    SubGhzRadioBrokerStatusV2 radio_status;
    subghz_radio_broker_get_status_v2(radio_broker, &radio_status);
    furi_string_cat_printf(
        output,
        "Radio broker: state=%s busy=%s device=%s external_power=%s owner=%s\n",
        tumo_acceptance_radio_state_name(radio_status.state),
        radio_status.base.busy ? "yes" : "no",
        tumo_acceptance_radio_device_name(radio_status.base.selected_device),
        radio_status.base.external_powered ? "yes" : "no",
        radio_status.base.owner[0] ? radio_status.base.owner : "none");
    furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);

    tumo_acceptance_append_info(
        output, "USB CLI log drops", "query with `log stats` after a USB log session");
    UNUSED(app);
}

static TumoAcceptanceResult tumo_acceptance_run_storage_test(TumoAcceptanceApp* app) {
    static const uint8_t probe[] = "TUMO_ACCEPTANCE_STORAGE_V1";
    uint8_t readback[sizeof(probe)] = {0};
    strlcpy(app->storage_detail, "not run", sizeof(app->storage_detail));

    if(storage_sd_status(app->storage) != FSE_OK) {
        strlcpy(app->storage_detail, "SD card is not mounted", sizeof(app->storage_detail));
        return TumoAcceptanceResultSkip;
    }
    if(!tumo_acceptance_mkdir(app->storage, TUMO_ACCEPTANCE_DATA_DIR)) {
        strlcpy(app->storage_detail, "private directory unavailable", sizeof(app->storage_detail));
        return TumoAcceptanceResultFail;
    }

    if(tumo_acceptance_path_exists(app->storage, TUMO_ACCEPTANCE_STORAGE_PROBE_PATH) &&
       storage_common_remove(app->storage, TUMO_ACCEPTANCE_STORAGE_PROBE_PATH) != FSE_OK) {
        strlcpy(
            app->storage_detail,
            "stale private probe cleanup failed",
            sizeof(app->storage_detail));
        return TumoAcceptanceResultFail;
    }

    File* file = storage_file_alloc(app->storage);
    bool opened =
        storage_file_open(file, TUMO_ACCEPTANCE_STORAGE_PROBE_PATH, FSAM_WRITE, FSOM_CREATE_NEW);
    bool write_ok = false;
    if(opened) {
        write_ok = storage_file_write(file, probe, sizeof(probe)) == sizeof(probe) &&
                   storage_file_sync(file);
        storage_file_close(file);
    }

    bool read_ok = false;
    if(write_ok && storage_file_open(
                       file, TUMO_ACCEPTANCE_STORAGE_PROBE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        read_ok = storage_file_read(file, readback, sizeof(readback)) == sizeof(readback) &&
                  memcmp(probe, readback, sizeof(probe)) == 0;
        storage_file_close(file);
    }
    storage_file_free(file);

    const bool cleanup_ok =
        storage_common_remove(app->storage, TUMO_ACCEPTANCE_STORAGE_PROBE_PATH) == FSE_OK &&
        !tumo_acceptance_path_exists(app->storage, TUMO_ACCEPTANCE_STORAGE_PROBE_PATH);
    if(!opened) {
        strlcpy(app->storage_detail, "CREATE_NEW failed", sizeof(app->storage_detail));
    } else if(!write_ok) {
        strlcpy(app->storage_detail, "write or sync failed", sizeof(app->storage_detail));
    } else if(!read_ok) {
        strlcpy(app->storage_detail, "read-back mismatch", sizeof(app->storage_detail));
    } else if(!cleanup_ok) {
        strlcpy(app->storage_detail, "private probe cleanup failed", sizeof(app->storage_detail));
    } else {
        strlcpy(
            app->storage_detail, "write, sync, read-back, cleanup", sizeof(app->storage_detail));
        return TumoAcceptanceResultPass;
    }
    return TumoAcceptanceResultFail;
}

static void
    tumo_acceptance_build_report(TumoAcceptanceApp* app, FuriString* output, const char* stage) {
    furi_string_reset(output);

    const Version* version = furi_hal_version_get_firmware_version();
    uint16_t api_major = 0;
    uint16_t api_minor = 0;
    furi_hal_info_get_api_version(&api_major, &api_minor);

    furi_string_cat_printf(
        output,
        "Tumoflip Hardware Acceptance Suite\n"
        "Schema: %s\n"
        "Suite: %s\n",
        TUMO_ACCEPTANCE_REPORT_SCHEMA,
        TUMO_ACCEPTANCE_SUITE_VERSION);
    furi_string_cat(output, "Generated: ");
    tumo_acceptance_append_iso_timestamp(output);
    furi_string_cat_printf(
        output,
        "\n\nFirmware\n"
        "Version: %s\n"
        "Commit: %s\n"
        "Branch: %s\n"
        "Origin: %s\n"
        "API: %u.%u\n"
        "Target: %u\n",
        version_get_version(version),
        version_get_githash(version),
        version_get_gitbranch(version),
        version_get_firmware_origin(version),
        api_major,
        api_minor,
        version_get_target(version));
    tumo_acceptance_append_result(
        output,
        "firmware dirty flag",
        !version_get_dirty_flag(version),
        version_get_dirty_flag(version) ? "build is dirty" : "clean build metadata");

    furi_string_cat(output, "\nStorage\n");
    const bool sd_ready = storage_sd_status(app->storage) == FSE_OK;
    tumo_acceptance_append_result(
        output, "SD card mounted", sd_ready, sd_ready ? "ready" : "not ready");
    uint64_t total_space = 0;
    uint64_t free_space = 0;
    const FS_Error fs_info =
        storage_common_fs_info(app->storage, STORAGE_EXT_PATH_PREFIX, &total_space, &free_space);
    char storage_detail[TUMO_ACCEPTANCE_DETAIL_SIZE];
    if(fs_info == FSE_OK) {
        snprintf(
            storage_detail,
            sizeof(storage_detail),
            "free=%llu total=%llu",
            (unsigned long long)free_space,
            (unsigned long long)total_space);
    } else {
        strlcpy(storage_detail, "capacity unavailable", sizeof(storage_detail));
    }
    tumo_acceptance_append_status(
        output,
        "SD free space",
        fs_info == FSE_OK ? TumoAcceptanceResultPass :
        sd_ready          ? TumoAcceptanceResultFail :
                            TumoAcceptanceResultSkip,
        storage_detail);
    tumo_acceptance_append_status(
        output, "private storage R/W", app->storage_result, app->storage_detail);
    const bool package_state =
        sd_ready && tumo_acceptance_path_exists(app->storage, TUMO_ACCEPTANCE_PACKAGE_STATE_PATH);
    tumo_acceptance_append_result(
        output,
        "package-state.txt",
        package_state,
        package_state ? TUMO_ACCEPTANCE_PACKAGE_STATE_PATH : "missing");

    furi_string_cat(output, "\nMemory\n");
    char heap_detail[96];
    snprintf(
        heap_detail,
        sizeof(heap_detail),
        "free=%lu min=%lu max_block=%lu",
        (unsigned long)memmgr_get_free_heap(),
        (unsigned long)memmgr_get_minimum_free_heap(),
        (unsigned long)memmgr_heap_get_max_free_block());
    tumo_acceptance_append_result(
        output, "heap headroom", memmgr_heap_get_max_free_block() >= 4096U, heap_detail);
    tumo_acceptance_append_runtime_snapshot(app, output, stage);

    furi_string_cat(output, "\nPassive hardware state\n");
    char battery_detail[TUMO_ACCEPTANCE_DETAIL_SIZE];
    snprintf(
        battery_detail,
        sizeof(battery_detail),
        "%u%% health=%u%% %.2fV %s",
        furi_hal_power_get_pct(),
        furi_hal_power_get_bat_health_pct(),
        (double)furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge),
        furi_hal_power_is_charging() ? "charging" : "battery");
    tumo_acceptance_append_result(
        output, "battery gauge", furi_hal_power_gauge_is_ok(), battery_detail);
    tumo_acceptance_append_status(
        output,
        "Display/input",
        app->self_test_ran ? TumoAcceptanceResultPass : TumoAcceptanceResultSkip,
        app->self_test_ran ? "safe test selected through GUI" : "run safe test first");
    tumo_acceptance_append_status(
        output, "NFC availability", TumoAcceptanceResultSkip, "not probed in FAP-only phase");
    tumo_acceptance_append_status(
        output, "GPIO baseline", TumoAcceptanceResultSkip, "no pins are driven in safe phase");
    tumo_acceptance_append_status(
        output, "BLE/AppBridge state", TumoAcceptanceResultSkip, "not probed in FAP-only phase");
    tumo_acceptance_append_info(output, "IR HAL", furi_hal_infrared_is_busy() ? "busy" : "free");
    tumo_acceptance_append_info(
        output, "USART", furi_hal_serial_control_is_busy(FuriHalSerialIdUsart) ? "busy" : "free");
    tumo_acceptance_append_info(
        output,
        "LPUART",
        furi_hal_serial_control_is_busy(FuriHalSerialIdLpuart) ? "busy" : "free");
    tumo_acceptance_append_info(
        output, "Sleep", furi_hal_power_sleep_available() ? "available" : "locked");
    tumo_acceptance_append_info(
        output, "OTG 5V", furi_hal_power_is_otg_enabled() ? "enabled" : "disabled");
    tumo_acceptance_append_info(
        output, "OTG fault", furi_hal_power_check_otg_fault() ? "yes" : "no");

    SubGhzRadioBroker* radio_broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    SubGhzRadioBrokerStatusV2 radio_status;
    subghz_radio_broker_get_status_v2(radio_broker, &radio_status);
    furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
    tumo_acceptance_append_status(
        output,
        "CC1101 broker baseline",
        radio_status.base.busy ? TumoAcceptanceResultSkip : TumoAcceptanceResultPass,
        radio_status.base.busy ? "owned by another app; left untouched" : "idle; no RF TX");

    furi_string_cat(output, "\nPackaged app paths\n");
    uint8_t missing_required = 0;
    for(size_t i = 0; i < COUNT_OF(tumo_acceptance_paths); i++) {
        const TumoAcceptancePathCheck* check = &tumo_acceptance_paths[i];
        const bool exists = tumo_acceptance_path_exists(app->storage, check->path);
        if(check->required && !exists) {
            missing_required++;
        }
        tumo_acceptance_append_result(output, check->label, exists, check->path);
    }

    furi_string_cat_printf(
        output,
        "\nSummary\n"
        "Missing required apps: %u\n"
        "Hardware-only transmit/receive tests: SKIP by design\n",
        missing_required);
    tumo_acceptance_append_result(
        output,
        "release smoke",
        sd_ready && !version_get_dirty_flag(version) && (missing_required == 0U) &&
            app->self_test_ran && app->storage_result == TumoAcceptanceResultPass,
        "safe FAP phase; optional hardware skips do not block");
}

static bool tumo_acceptance_write_text_file(Storage* storage, const char* path, const char* text) {
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW);
    if(ok) {
        const size_t size = strlen(text);
        ok = storage_file_write(file, text, size) == size && storage_file_sync(file);
        storage_file_close(file);
    }
    storage_file_free(file);
    return ok;
}

static void tumo_acceptance_export_report(TumoAcceptanceApp* app) {
    char timestamp[32];
    char path[TUMO_ACCEPTANCE_REPORT_PATH_SIZE];
    tumo_acceptance_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(path, sizeof(path), TUMO_ACCEPTANCE_DATA_DIR "/acceptance_%s.txt", timestamp);

    FuriString* report = furi_string_alloc();
    tumo_acceptance_build_report(app, report, "export");

    const bool dir_ok = tumo_acceptance_mkdir(app->storage, TUMO_ACCEPTANCE_DATA_DIR);
    const bool write_ok = dir_ok && tumo_acceptance_write_text_file(
                                        app->storage, path, furi_string_get_cstr(report));

    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Acceptance Export\n\n"
        "Result: %s\n"
        "Path:\n%s\n\n",
        write_ok ? "saved" : (dir_ok ? "write failed" : "mkdir failed"),
        path);
    furi_string_cat(app->text, report);
    furi_string_free(report);
}

static void tumo_acceptance_show_text(TumoAcceptanceApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoAcceptanceViewText);
}

static void tumo_acceptance_build_about(TumoAcceptanceApp* app) {
    furi_string_reset(app->text);
    furi_string_cat(
        app->text,
        "Tumo Acceptance\n\n"
        "Safe release checks for Tumoflip dev builds.\n\n"
        "The private SD probe is created, read back, and deleted. Existing files are never overwritten.\n\n"
        "No IR/Sub-GHz TX, NFC writes, GPIO drive, OTG enable, or child app launch.");
}

static void tumo_acceptance_menu_callback(void* context, uint32_t index) {
    TumoAcceptanceApp* app = context;

    switch(index) {
    case TumoAcceptanceActionRun:
        app->self_test_ran = true;
        app->storage_result = tumo_acceptance_run_storage_test(app);
        tumo_acceptance_build_report(app, app->text, "run");
        break;
    case TumoAcceptanceActionExport:
        tumo_acceptance_export_report(app);
        break;
    case TumoAcceptanceActionAbout:
        tumo_acceptance_build_about(app);
        break;
    default:
        furi_string_reset(app->text);
        furi_string_cat(app->text, "Unsupported action.\n");
        break;
    }

    tumo_acceptance_show_text(app);
}

static bool tumo_acceptance_back_callback(void* context) {
    UNUSED(context);
    view_dispatcher_stop(((TumoAcceptanceApp*)context)->view_dispatcher);
    return true;
}

static uint32_t tumo_acceptance_text_previous_callback(void* context) {
    UNUSED(context);
    return TumoAcceptanceViewMenu;
}

static TumoAcceptanceApp* tumo_acceptance_alloc(void) {
    TumoAcceptanceApp* app = malloc(sizeof(TumoAcceptanceApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_box = text_box_alloc();
    app->text = furi_string_alloc();
    app->self_test_ran = false;
    app->storage_result = TumoAcceptanceResultSkip;
    strlcpy(app->storage_detail, "run safe test first", sizeof(app->storage_detail));

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, tumo_acceptance_back_callback);

    submenu_set_header(app->submenu, "Tumo Acceptance");
    submenu_add_item(
        app->submenu, "Run Safe Test", TumoAcceptanceActionRun, tumo_acceptance_menu_callback, app);
    submenu_add_item(
        app->submenu,
        "Export Report",
        TumoAcceptanceActionExport,
        tumo_acceptance_menu_callback,
        app);
    submenu_add_item(
        app->submenu, "About", TumoAcceptanceActionAbout, tumo_acceptance_menu_callback, app);

    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(
        text_box_get_view(app->text_box), tumo_acceptance_text_previous_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, TumoAcceptanceViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, TumoAcceptanceViewText, text_box_get_view(app->text_box));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoAcceptanceViewMenu);
    return app;
}

static void tumo_acceptance_free(TumoAcceptanceApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, TumoAcceptanceViewText);
    view_dispatcher_remove_view(app->view_dispatcher, TumoAcceptanceViewMenu);
    text_box_free(app->text_box);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->text);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumo_acceptance_suite_app(void* context) {
    UNUSED(context);
    TumoAcceptanceApp* app = tumo_acceptance_alloc();
    view_dispatcher_run(app->view_dispatcher);
    tumo_acceptance_free(app);
    return 0;
}
