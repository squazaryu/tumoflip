#include <furi.h>
#include <furi_hal_i2c.h>
#include <furi_hal_infrared.h>
#include <furi_hal_power.h>
#include <furi_hal_rtc.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <loader/loader.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODULE_ONE_COCKPIT_IR_FREQUENCY 38000U
#define MODULE_ONE_COCKPIT_IR_DUTY      0.33f
#define MODULE_ONE_COCKPIT_IR_BURST_US  80000U
#define MODULE_ONE_COCKPIT_IR_TIMEOUT_MS 250U
#define MODULE_ONE_COCKPIT_I2C_TIMEOUT_MS 2U
#define MODULE_ONE_COCKPIT_UART_BAUD 115200U
#define MODULE_ONE_COCKPIT_DATA_DIR EXT_PATH("apps_data/module_one_cockpit")
#define MODULE_ONE_COCKPIT_REPORT_PATH_SIZE 128U

typedef enum {
    ModuleOneCockpitViewMenu,
    ModuleOneCockpitViewText,
} ModuleOneCockpitView;

typedef enum {
    ModuleOneCockpitActionStatus,
    ModuleOneCockpitActionIrBlink,
    ModuleOneCockpitActionI2cScan,
    ModuleOneCockpitActionUartStatus,
    ModuleOneCockpitActionLaunchBleGattLab,
    ModuleOneCockpitActionLaunchSensorLogger,
    ModuleOneCockpitActionEsp32Ping,
    ModuleOneCockpitActionLaunchIrLab,
    ModuleOneCockpitActionLaunchXRemote,
    ModuleOneCockpitActionLaunchWifiMapper,
    ModuleOneCockpitActionLaunchArfHub,
    ModuleOneCockpitActionLaunchArfStatus,
    ModuleOneCockpitActionLaunchGpio,
    ModuleOneCockpitActionLaunchSubGhz,
    ModuleOneCockpitActionExportDiagnostics,
    ModuleOneCockpitActionAbout,
    ModuleOneCockpitActionCount,
} ModuleOneCockpitAction;

typedef enum {
    ModuleOneCockpitBlockIr,
    ModuleOneCockpitBlockEsp32,
    ModuleOneCockpitBlockGps,
    ModuleOneCockpitBlockBme280,
    ModuleOneCockpitBlockSensor,
    ModuleOneCockpitBlockBle,
    ModuleOneCockpitBlockNrf24,
    ModuleOneCockpitBlockCc1101,
    ModuleOneCockpitBlockSystem,
} ModuleOneCockpitBlock;

typedef struct {
    const char* label;
    ModuleOneCockpitAction action;
} ModuleOneCockpitMenuItem;

typedef struct {
    ModuleOneCockpitAction action;
    ModuleOneCockpitBlock block;
    const char* name;
    const char* target;
    const char* args;
    bool storage_backed;
} ModuleOneCockpitLaunchTarget;

typedef struct {
    bool continuous;
    uint32_t remaining_us;
} ModuleOneCockpitIrTx;

typedef struct {
    bool found;
    uint8_t address;
} ModuleOneCockpitI2cProbe;

typedef struct {
    Gui* gui;
    Storage* storage;
    Loader* loader;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    FuriString* text;
    uint32_t selected_item;
} ModuleOneCockpitApp;

static const ModuleOneCockpitMenuItem module_one_cockpit_menu[] = {
    {"Status", ModuleOneCockpitActionStatus},
    {"IR: Blink Test", ModuleOneCockpitActionIrBlink},
    {"IR: Tumo IR Lab", ModuleOneCockpitActionLaunchIrLab},
    {"IR: Tumo XRemote", ModuleOneCockpitActionLaunchXRemote},
    {"ESP32: AT Ping", ModuleOneCockpitActionEsp32Ping},
    {"ESP32: WiFi Mapper", ModuleOneCockpitActionLaunchWifiMapper},
    {"GPS/UART: Status", ModuleOneCockpitActionUartStatus},
    {"BLE: GATT Lab", ModuleOneCockpitActionLaunchBleGattLab},
    {"BME280/I2C: Scan", ModuleOneCockpitActionI2cScan},
    {"Sensors: Logger", ModuleOneCockpitActionLaunchSensorLogger},
    {"CC1101: ARF Full", ModuleOneCockpitActionLaunchArfHub},
    {"CC1101: ARF Status", ModuleOneCockpitActionLaunchArfStatus},
    {"System: GPIO", ModuleOneCockpitActionLaunchGpio},
    {"System: Sub-GHz", ModuleOneCockpitActionLaunchSubGhz},
    {"Diagnostics: Export", ModuleOneCockpitActionExportDiagnostics},
    {"About", ModuleOneCockpitActionAbout},
};

static const ModuleOneCockpitLaunchTarget module_one_cockpit_targets[] = {
    {
        ModuleOneCockpitActionLaunchIrLab,
        ModuleOneCockpitBlockIr,
        "Tumo IR Lab",
        EXT_PATH("apps/Module One/IR Blaster/tumo_ir_lab.fap"),
        NULL,
        true,
    },
    {
        ModuleOneCockpitActionLaunchXRemote,
        ModuleOneCockpitBlockIr,
        "Tumo XRemote",
        EXT_PATH("apps/Module One/IR Blaster/tumoflip_xremote.fap"),
        NULL,
        true,
    },
    {
        ModuleOneCockpitActionLaunchWifiMapper,
        ModuleOneCockpitBlockEsp32,
        "WiFi Mapper",
        EXT_PATH("apps/Module One/ESP32 Wi-Fi/wifi_mapper.fap"),
        NULL,
        true,
    },
    {
        ModuleOneCockpitActionLaunchBleGattLab,
        ModuleOneCockpitBlockBle,
        "BLE GATT Lab",
        EXT_PATH("apps/Module One/BLE/ble_gatt_lab.fap"),
        NULL,
        true,
    },
    {
        ModuleOneCockpitActionLaunchSensorLogger,
        ModuleOneCockpitBlockSensor,
        "Sensor Logger",
        EXT_PATH("apps/Module One/module_one_sensor_logger.fap"),
        NULL,
        true,
    },
    {
        ModuleOneCockpitActionLaunchArfHub,
        ModuleOneCockpitBlockCc1101,
        "ARF Sub-GHz Full",
        EXT_PATH("apps/ARF Tools/arf_subghz_full.fap"),
        NULL,
        true,
    },
    {
        ModuleOneCockpitActionLaunchArfStatus,
        ModuleOneCockpitBlockCc1101,
        "ARF Status",
        EXT_PATH("apps_data/arf_subghz_full/modules/arf_status.fap"),
        NULL,
        true,
    },
    {
        ModuleOneCockpitActionLaunchGpio,
        ModuleOneCockpitBlockSystem,
        "GPIO",
        "GPIO",
        NULL,
        false,
    },
    {
        ModuleOneCockpitActionLaunchSubGhz,
        ModuleOneCockpitBlockSystem,
        "Sub-GHz",
        "Sub-GHz",
        NULL,
        false,
    },
};

static bool module_one_cockpit_path_exists(Storage* storage, const char* path) {
    return storage_common_stat(storage, path, NULL) == FSE_OK;
}

static bool module_one_cockpit_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static void module_one_cockpit_format_filename_timestamp(char* output, size_t output_size) {
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

static void module_one_cockpit_append_iso_timestamp(FuriString* output) {
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

static const ModuleOneCockpitLaunchTarget*
    module_one_cockpit_find_target(ModuleOneCockpitAction action) {
    for(size_t i = 0; i < COUNT_OF(module_one_cockpit_targets); i++) {
        if(module_one_cockpit_targets[i].action == action) {
            return &module_one_cockpit_targets[i];
        }
    }

    return NULL;
}

static const char* module_one_cockpit_block_label(ModuleOneCockpitBlock block) {
    switch(block) {
    case ModuleOneCockpitBlockIr:
        return "IR";
    case ModuleOneCockpitBlockEsp32:
        return "ESP32";
    case ModuleOneCockpitBlockGps:
        return "GPS";
    case ModuleOneCockpitBlockBme280:
        return "BME280";
    case ModuleOneCockpitBlockSensor:
        return "Sensor";
    case ModuleOneCockpitBlockBle:
        return "BLE";
    case ModuleOneCockpitBlockNrf24:
        return "NRF24";
    case ModuleOneCockpitBlockCc1101:
        return "CC1101";
    case ModuleOneCockpitBlockSystem:
        return "System";
    default:
        return "?";
    }
}

static void module_one_cockpit_show_text(ModuleOneCockpitApp* app, const char* title) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    UNUSED(title);
    view_dispatcher_switch_to_view(app->view_dispatcher, ModuleOneCockpitViewText);
}

static void module_one_cockpit_append_app_status(
    ModuleOneCockpitApp* app,
    FuriString* output,
    const ModuleOneCockpitLaunchTarget* target) {
    const bool available = !target->storage_backed ||
                           module_one_cockpit_path_exists(app->storage, target->target);
    furi_string_cat_printf(
        output,
        "%s / %s: %s\n",
        module_one_cockpit_block_label(target->block),
        target->name,
        available ? "OK" : "missing");
    furi_string_cat_printf(
        output,
        "  %s%s\n",
        target->storage_backed ? "" : "loader: ",
        target->target);
}

static bool module_one_cockpit_i2c_ready(uint8_t addr_7bit) {
    return furi_hal_i2c_is_device_ready(
        &furi_hal_i2c_handle_external, (uint8_t)(addr_7bit << 1), MODULE_ONE_COCKPIT_I2C_TIMEOUT_MS);
}

static ModuleOneCockpitI2cProbe module_one_cockpit_probe_bme280(void) {
    ModuleOneCockpitI2cProbe probe = {
        .found = false,
        .address = 0,
    };

    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    if(module_one_cockpit_i2c_ready(0x76)) {
        probe.found = true;
        probe.address = 0x76;
    } else if(module_one_cockpit_i2c_ready(0x77)) {
        probe.found = true;
        probe.address = 0x77;
    }
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);
    return probe;
}

static void module_one_cockpit_append_power_report(FuriString* output) {
    const uint32_t usb_mv = (uint32_t)(furi_hal_power_get_usb_voltage() * 1000.0f);

    furi_string_cat_printf(
        output,
        "Power\n"
        "Battery: %u%%\n"
        "Battery health: %u%%\n"
        "Charging: %s\n"
        "USB: %lu.%02lu V\n"
        "OTG 5V: %s\n"
        "OTG fault: %s\n\n",
        furi_hal_power_get_pct(),
        furi_hal_power_get_bat_health_pct(),
        furi_hal_power_is_charging() ? "yes" : "no",
        (unsigned long)(usb_mv / 1000U),
        (unsigned long)((usb_mv % 1000U) / 10U),
        furi_hal_power_is_otg_enabled() ? "on" : "off",
        furi_hal_power_check_otg_fault() ? "yes" : "no");
}

static void module_one_cockpit_append_runtime_report(FuriString* output) {
    const bool ir_busy = furi_hal_infrared_is_busy();
    const bool usart_busy = furi_hal_serial_control_is_busy(FuriHalSerialIdUsart);
    const bool lpuart_busy = furi_hal_serial_control_is_busy(FuriHalSerialIdLpuart);
    const ModuleOneCockpitI2cProbe bme280 = module_one_cockpit_probe_bme280();

    furi_string_cat_printf(
        output,
        "Runtime health\n"
        "IR HAL: %s\n"
        "USART: %s\n"
        "LPUART: %s\n"
        "Sleep: %s\n",
        ir_busy ? "busy" : "free",
        usart_busy ? "busy" : "free",
        lpuart_busy ? "busy" : "free",
        furi_hal_power_sleep_available() ? "available" : "locked");

    if(bme280.found) {
        furi_string_cat_printf(output, "BME280: present at 0x%02X\n", bme280.address);
    } else {
        furi_string_cat_printf(output, "BME280: not detected on 0x76/0x77\n");
    }

    furi_string_cat_printf(
        output,
        "ESP32/GPS: UART free means probe-safe; use dedicated screens for active checks\n"
        "BLE: use BLE GATT Lab for App Bridge ping/status/echo\n"
        "NRF24: unknown, no safe passive probe yet\n"
        "CC1101: unknown, use ARF/Sub-GHz status before transmit\n\n");
}

static void module_one_cockpit_append_app_report(ModuleOneCockpitApp* app, FuriString* output) {
    furi_string_cat_printf(output, "Apps\n");
    for(size_t i = 0; i < COUNT_OF(module_one_cockpit_targets); i++) {
        module_one_cockpit_append_app_status(app, output, &module_one_cockpit_targets[i]);
    }
}

static void module_one_cockpit_build_report(ModuleOneCockpitApp* app, FuriString* output) {
    furi_string_reset(output);
    furi_string_cat_printf(output, "Module One Cockpit Pro\n");
    furi_string_cat_printf(output, "Generated: ");
    module_one_cockpit_append_iso_timestamp(output);
    furi_string_cat_printf(output, "\n\n");

    module_one_cockpit_append_power_report(output);
    module_one_cockpit_append_runtime_report(output);
    module_one_cockpit_append_app_report(app, output);

    furi_string_cat_printf(
        output,
        "\nHardware blocks\n"
        "IR: use IR Blink or Tumo IR Lab\n"
        "ESP32: use UART/AT or WiFi Mapper\n"
        "BLE: use BLE GATT Lab for App Bridge diagnostics\n"
        "GPS/BME280: use Sensor Logger\n"
        "NRF24: passive detection is not implemented yet\n"
        "CC1101: use ARF Sub-GHz Full or Sub-GHz status\n");
}

static void module_one_cockpit_build_status(ModuleOneCockpitApp* app) {
    module_one_cockpit_build_report(app, app->text);
}

static FuriHalInfraredTxGetDataState
    module_one_cockpit_ir_tx_callback(void* context, uint32_t* duration, bool* level) {
    ModuleOneCockpitIrTx* tx = context;
    *level = true;

    if(tx->continuous) {
        *duration = MODULE_ONE_COCKPIT_IR_BURST_US;
        return FuriHalInfraredTxGetDataStateDone;
    }

    *duration = tx->remaining_us;
    tx->remaining_us = 0;
    return FuriHalInfraredTxGetDataStateLastDone;
}

static void module_one_cockpit_ir_done_callback(void* context) {
    bool* done = context;
    *done = true;
}

static void module_one_cockpit_run_ir_blink(ModuleOneCockpitApp* app) {
    furi_string_reset(app->text);
    furi_string_cat_printf(app->text, "IR Blink Test\n\n");

    if(furi_hal_infrared_is_busy()) {
        furi_string_cat_printf(app->text, "IR HAL is busy.\nClose other IR apps and retry.\n");
        return;
    }

    volatile bool done = false;
    ModuleOneCockpitIrTx tx = {
        .continuous = false,
        .remaining_us = MODULE_ONE_COCKPIT_IR_BURST_US,
    };

    const FuriHalInfraredTxPin output = furi_hal_infrared_detect_tx_output();
    furi_hal_infrared_set_tx_output(output);
    furi_hal_infrared_async_tx_set_data_isr_callback(module_one_cockpit_ir_tx_callback, &tx);
    furi_hal_infrared_async_tx_set_signal_sent_isr_callback(
        module_one_cockpit_ir_done_callback, (void*)&done);
    furi_hal_infrared_async_tx_start(
        MODULE_ONE_COCKPIT_IR_FREQUENCY, MODULE_ONE_COCKPIT_IR_DUTY);

    const uint32_t start = furi_get_tick();
    const uint32_t timeout = furi_ms_to_ticks(MODULE_ONE_COCKPIT_IR_TIMEOUT_MS);
    while(!done && ((furi_get_tick() - start) < timeout)) {
        furi_delay_ms(5);
    }

    furi_hal_infrared_async_tx_stop();
    furi_hal_infrared_async_tx_set_signal_sent_isr_callback(NULL, NULL);
    furi_hal_infrared_async_tx_set_data_isr_callback(NULL, NULL);

    furi_string_cat_printf(
        app->text,
        "Output: %s\n"
        "Carrier: 38 kHz\n"
        "Duty: 33%%\n"
        "Burst: 80 ms\n"
        "Result: %s\n",
        output == FuriHalInfraredTxPinExtPA7 ? "MOD1" : "INT",
        done ? "sent" : "timeout");
}

static void module_one_cockpit_run_i2c_scan(ModuleOneCockpitApp* app) {
    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "I2C Scan\n\n"
        "Bus: external PC0/PC1\n"
        "Clock: 100 kHz\n\n");

    uint8_t found = 0;
    bool bme280_found = false;
    furi_hal_i2c_acquire(&furi_hal_i2c_handle_external);
    for(uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if(module_one_cockpit_i2c_ready(addr)) {
            furi_string_cat_printf(app->text, "0x%02X", addr);
            if((addr == 0x76) || (addr == 0x77)) {
                furi_string_cat_printf(app->text, " BME280?");
                bme280_found = true;
            }
            furi_string_cat_printf(app->text, "\n");
            found++;
        }
    }
    furi_hal_i2c_release(&furi_hal_i2c_handle_external);

    if(found == 0U) {
        furi_string_cat_printf(app->text, "No devices responded.\n");
    }

    furi_string_cat_printf(
        app->text,
        "\nFound: %u\nBME280: %s\n",
        found,
        bme280_found ? "possible" : "not detected");
}

static bool module_one_cockpit_write_text_file(
    Storage* storage,
    const char* path,
    const char* text) {
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        const size_t size = strlen(text);
        ok = storage_file_write(file, text, size) == size;
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static void module_one_cockpit_export_report(ModuleOneCockpitApp* app) {
    char timestamp[32];
    char path[MODULE_ONE_COCKPIT_REPORT_PATH_SIZE];
    module_one_cockpit_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        path,
        sizeof(path),
        MODULE_ONE_COCKPIT_DATA_DIR "/diagnostics_%s.txt",
        timestamp);

    FuriString* report = furi_string_alloc();
    module_one_cockpit_build_report(app, report);

    const bool dir_ok = module_one_cockpit_mkdir(app->storage, MODULE_ONE_COCKPIT_DATA_DIR);
    const bool write_ok =
        dir_ok && module_one_cockpit_write_text_file(app->storage, path, furi_string_get_cstr(report));

    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Diagnostics Export\n\n"
        "Result: %s\n"
        "Path:\n%s\n\n",
        write_ok ? "saved" : (dir_ok ? "write failed" : "mkdir failed"),
        path);
    furi_string_cat(app->text, report);
    furi_string_free(report);
}

static void module_one_cockpit_build_uart_status(ModuleOneCockpitApp* app) {
    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "UART Status\n\n"
        "USART: %s\n"
        "LPUART: %s\n\n"
        "ESP32 Wi-Fi and GPS share the external UART class on Module One.\n"
        "Busy means another app/service owns the port.\n",
        furi_hal_serial_control_is_busy(FuriHalSerialIdUsart) ? "busy" : "free",
        furi_hal_serial_control_is_busy(FuriHalSerialIdLpuart) ? "busy" : "free");
}

static void module_one_cockpit_run_esp32_ping(ModuleOneCockpitApp* app) {
    furi_string_reset(app->text);
    furi_string_cat_printf(app->text, "ESP32 AT Ping\n\n");

    FuriHalSerialHandle* serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!serial) {
        furi_string_cat_printf(app->text, "USART is busy.\nClose WiFi/GPS tools and retry.\n");
        return;
    }

    furi_hal_serial_init(serial, MODULE_ONE_COCKPIT_UART_BAUD);
    furi_hal_serial_configure_framing(
        serial, FuriHalSerialDataBits8, FuriHalSerialParityNone, FuriHalSerialStopBits1);
    furi_hal_serial_tx(serial, (const uint8_t*)"AT\r\n", 4);
    furi_hal_serial_tx_wait_complete(serial);
    furi_hal_serial_deinit(serial);
    furi_hal_serial_control_release(serial);

    furi_string_cat_printf(
        app->text,
        "Sent: AT\\r\\n\n"
        "Baud: 115200\n"
        "Result: tx complete\n\n"
        "This verifies the UART path is free and writable; use WiFi Mapper for live ESP32 data.\n");
}

static void module_one_cockpit_build_about(ModuleOneCockpitApp* app) {
    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Module One Cockpit\n\n"
        "A single entry point for Module One status, safe diagnostics, and launch shortcuts.\n\n"
        "It does not require every module to be connected. Missing apps and silent buses are reported as unavailable.\n");
}

static void module_one_cockpit_launch_or_report(
    ModuleOneCockpitApp* app,
    const ModuleOneCockpitLaunchTarget* target) {
    if(target->storage_backed && !module_one_cockpit_path_exists(app->storage, target->target)) {
        furi_string_reset(app->text);
        furi_string_cat_printf(
            app->text,
            "%s\n\nNot installed:\n%s\n",
            target->name,
            target->target);
        module_one_cockpit_show_text(app, target->name);
        return;
    }

    loader_clear_launch_queue(app->loader);
    loader_enqueue_launch(app->loader, target->target, target->args, LoaderDeferredLaunchFlagGui);

    FuriString* self_path = furi_string_alloc();
    if(loader_get_application_launch_path(app->loader, self_path)) {
        char selected_item_arg[8];
        snprintf(selected_item_arg, sizeof(selected_item_arg), "%lu", (unsigned long)app->selected_item);
        loader_enqueue_launch(
            app->loader,
            furi_string_get_cstr(self_path),
            selected_item_arg,
            LoaderDeferredLaunchFlagGui);
    }
    furi_string_free(self_path);

    view_dispatcher_stop(app->view_dispatcher);
}

static void module_one_cockpit_submenu_callback(void* context, uint32_t index) {
    ModuleOneCockpitApp* app = context;
    furi_check(index < COUNT_OF(module_one_cockpit_menu));
    app->selected_item = index;

    const ModuleOneCockpitAction action = module_one_cockpit_menu[index].action;
    const ModuleOneCockpitLaunchTarget* target = module_one_cockpit_find_target(action);
    if(target) {
        module_one_cockpit_launch_or_report(app, target);
        return;
    }

    switch(action) {
    case ModuleOneCockpitActionStatus:
        module_one_cockpit_build_status(app);
        break;
    case ModuleOneCockpitActionIrBlink:
        module_one_cockpit_run_ir_blink(app);
        break;
    case ModuleOneCockpitActionI2cScan:
        module_one_cockpit_run_i2c_scan(app);
        break;
    case ModuleOneCockpitActionUartStatus:
        module_one_cockpit_build_uart_status(app);
        break;
    case ModuleOneCockpitActionEsp32Ping:
        module_one_cockpit_run_esp32_ping(app);
        break;
    case ModuleOneCockpitActionExportDiagnostics:
        module_one_cockpit_export_report(app);
        break;
    case ModuleOneCockpitActionAbout:
        module_one_cockpit_build_about(app);
        break;
    default:
        furi_string_reset(app->text);
        furi_string_cat_printf(app->text, "Unsupported action.\n");
        break;
    }

    module_one_cockpit_show_text(app, module_one_cockpit_menu[index].label);
}

static bool module_one_cockpit_back_callback(void* context) {
    ModuleOneCockpitApp* app = context;
    UNUSED(app);
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static uint32_t module_one_cockpit_text_previous_callback(void* context) {
    ModuleOneCockpitApp* app = context;
    UNUSED(app);
    return ModuleOneCockpitViewMenu;
}

static ModuleOneCockpitApp* module_one_cockpit_alloc(uint32_t selected_item) {
    ModuleOneCockpitApp* app = malloc(sizeof(ModuleOneCockpitApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->loader = furi_record_open(RECORD_LOADER);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_box = text_box_alloc();
    app->text = furi_string_alloc();
    app->selected_item = (selected_item < COUNT_OF(module_one_cockpit_menu)) ? selected_item : 0;

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, module_one_cockpit_back_callback);

    for(size_t i = 0; i < COUNT_OF(module_one_cockpit_menu); i++) {
        submenu_add_item(
            app->submenu,
            module_one_cockpit_menu[i].label,
            i,
            module_one_cockpit_submenu_callback,
            app);
    }
    submenu_set_selected_item(app->submenu, app->selected_item);

    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(
        text_box_get_view(app->text_box), module_one_cockpit_text_previous_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, ModuleOneCockpitViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, ModuleOneCockpitViewText, text_box_get_view(app->text_box));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, ModuleOneCockpitViewMenu);
    return app;
}

static void module_one_cockpit_free(ModuleOneCockpitApp* app) {
    furi_assert(app);
    view_dispatcher_remove_view(app->view_dispatcher, ModuleOneCockpitViewText);
    view_dispatcher_remove_view(app->view_dispatcher, ModuleOneCockpitViewMenu);
    text_box_free(app->text_box);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->text);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t module_one_cockpit_app(void* context) {
    uint32_t selected_item = 0;
    if(context) {
        selected_item = strtoul(context, NULL, 10);
    }

    ModuleOneCockpitApp* app = module_one_cockpit_alloc(selected_item);
    view_dispatcher_run(app->view_dispatcher);
    module_one_cockpit_free(app);
    return 0;
}
