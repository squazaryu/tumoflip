#include <furi.h>
#include <furi_hal_infrared.h>
#include <flipper_format/flipper_format.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/variable_item_list.h>
#include <gui/view_dispatcher.h>
#include <infrared.h>
#include <infrared_transmit.h>
#include <storage/storage.h>

#include <string.h>

#define TAG "TumoIrLab"

#define TUMO_IR_LAB_DIR         EXT_PATH("apps_data/tumo_ir_lab")
#define TUMO_IR_LAB_PRESET_PATH EXT_PATH("apps_data/tumo_ir_lab/preset.irlab")
#define TUMO_IR_LAB_RAW_MAX     64U

typedef enum {
    TumoIrLabViewMain,
    TumoIrLabViewSettings,
    TumoIrLabViewText,
} TumoIrLabView;

typedef enum {
    TumoIrLabMenuCarrier,
    TumoIrLabMenuRaw,
    TumoIrLabMenuProtocol,
    TumoIrLabMenuSettings,
    TumoIrLabMenuSave,
    TumoIrLabMenuLoad,
    TumoIrLabMenuAbout,
} TumoIrLabMenu;

typedef enum {
    TumoIrLabOutputAuto,
    TumoIrLabOutputInternal,
    TumoIrLabOutputExtPA7,
    TumoIrLabOutputCount,
} TumoIrLabOutput;

typedef struct {
    const char* label;
    uint32_t hz;
} TumoIrLabFrequency;

typedef struct {
    const char* label;
    uint8_t percent;
} TumoIrLabDuty;

typedef struct {
    const char* label;
    uint32_t ms;
} TumoIrLabBurst;

typedef struct {
    const char* label;
    uint8_t count;
} TumoIrLabRepeat;

typedef struct {
    const char* label;
    InfraredProtocol protocol;
    uint32_t address;
    uint32_t command;
} TumoIrLabProtocolSample;

typedef struct {
    uint32_t duration_us;
    bool consumed;
} TumoIrLabCarrierTx;

typedef struct {
    Gui* gui;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    VariableItemList* settings;
    TextBox* text_box;
    FuriString* text;

    VariableItem* output_item;
    VariableItem* frequency_item;
    VariableItem* duty_item;
    VariableItem* burst_item;
    VariableItem* repeat_item;
    VariableItem* protocol_item;

    uint8_t output_index;
    uint8_t frequency_index;
    uint8_t duty_index;
    uint8_t burst_index;
    uint8_t repeat_index;
    uint8_t protocol_index;

    uint32_t raw_timings[TUMO_IR_LAB_RAW_MAX];
    uint32_t raw_count;
    bool raw_start_from_mark;
} TumoIrLabApp;

static const char* const tumo_ir_lab_output_labels[TumoIrLabOutputCount] = {
    "Auto",
    "Internal",
    "Module One",
};

static const TumoIrLabFrequency tumo_ir_lab_frequencies[] = {
    {"36 kHz", 36000},
    {"38 kHz", 38000},
    {"40 kHz", 40000},
    {"56 kHz", 56000},
};

static const TumoIrLabDuty tumo_ir_lab_duties[] = {
    {"25%", 25},
    {"33%", 33},
    {"50%", 50},
};

static const TumoIrLabBurst tumo_ir_lab_bursts[] = {
    {"100 ms", 100},
    {"250 ms", 250},
    {"500 ms", 500},
    {"1000 ms", 1000},
};

static const TumoIrLabRepeat tumo_ir_lab_repeats[] = {
    {"1x", 1},
    {"3x", 3},
    {"5x", 5},
};

static const TumoIrLabProtocolSample tumo_ir_lab_protocols[] = {
    {"NEC", InfraredProtocolNEC, 0x00, 0x45},
    {"SIRC", InfraredProtocolSIRC, 0x01, 0x15},
    {"RC5", InfraredProtocolRC5, 0x00, 0x10},
};

static const uint32_t tumo_ir_lab_default_raw[] = {
    9000,
    4500,
    560,
    560,
    560,
    560,
    560,
    1690,
    560,
    560,
    560,
    1690,
    560,
    40000,
};

static uint8_t tumo_ir_lab_clamp_index(uint32_t index, size_t count, uint8_t fallback) {
    return (index < count) ? index : fallback;
}

static float tumo_ir_lab_duty_value(const TumoIrLabApp* app) {
    return (float)tumo_ir_lab_duties[app->duty_index].percent / 100.0f;
}

static FuriHalInfraredTxPin tumo_ir_lab_resolve_output(const TumoIrLabApp* app) {
    if(app->output_index == TumoIrLabOutputExtPA7) {
        return FuriHalInfraredTxPinExtPA7;
    } else if(app->output_index == TumoIrLabOutputInternal) {
        return FuriHalInfraredTxPinInternal;
    }

    return furi_hal_infrared_detect_tx_output();
}

static const char* tumo_ir_lab_pin_label(FuriHalInfraredTxPin pin) {
    return pin == FuriHalInfraredTxPinExtPA7 ? "Module One PA7" : "Internal IR";
}

static void tumo_ir_lab_set_text(TumoIrLabApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, TumoIrLabViewText);
}

static void tumo_ir_lab_status_header(TumoIrLabApp* app, const char* title) {
    furi_string_printf(
        app->text,
        "%s\n\nOutput: %s\nFreq: %s\nDuty: %s\n",
        title,
        tumo_ir_lab_output_labels[app->output_index],
        tumo_ir_lab_frequencies[app->frequency_index].label,
        tumo_ir_lab_duties[app->duty_index].label);
}

static FuriHalInfraredTxGetDataState
    tumo_ir_lab_carrier_callback(void* context, uint32_t* duration, bool* level) {
    TumoIrLabCarrierTx* tx = context;
    furi_assert(tx);
    furi_assert(duration);
    furi_assert(level);

    *level = true;
    *duration = tx->consumed ? 0 : tx->duration_us;
    tx->consumed = true;
    return FuriHalInfraredTxGetDataStateLastDone;
}

static bool tumo_ir_lab_send_carrier(
    uint32_t frequency,
    float duty,
    uint32_t burst_ms,
    FuriHalInfraredTxPin output) {
    if(furi_hal_infrared_is_busy()) {
        return false;
    }

    TumoIrLabCarrierTx tx = {
        .duration_us = burst_ms * 1000U,
        .consumed = false,
    };

    furi_hal_infrared_set_tx_output(output);
    furi_hal_infrared_async_tx_set_data_isr_callback(tumo_ir_lab_carrier_callback, &tx);
    furi_hal_infrared_async_tx_start(frequency, duty);
    furi_hal_infrared_async_tx_wait_termination();
    furi_hal_infrared_set_tx_output(FuriHalInfraredTxPinInternal);

    return true;
}

static void tumo_ir_lab_run_carrier(TumoIrLabApp* app) {
    const FuriHalInfraredTxPin output = tumo_ir_lab_resolve_output(app);
    const uint32_t frequency = tumo_ir_lab_frequencies[app->frequency_index].hz;
    const uint32_t burst_ms = tumo_ir_lab_bursts[app->burst_index].ms;
    const bool ok = tumo_ir_lab_send_carrier(
        frequency, tumo_ir_lab_duty_value(app), burst_ms, output);

    tumo_ir_lab_status_header(app, "Carrier burst");
    furi_string_cat_printf(
        app->text,
        "Burst: %lu ms\nPin: %s\n\n%s",
        burst_ms,
        tumo_ir_lab_pin_label(output),
        ok ? "TX complete." : "IR is busy. Close other IR apps.");
    tumo_ir_lab_set_text(app);
}

static void tumo_ir_lab_run_raw(TumoIrLabApp* app) {
    const FuriHalInfraredTxPin output = tumo_ir_lab_resolve_output(app);
    const uint32_t frequency = tumo_ir_lab_frequencies[app->frequency_index].hz;
    const uint8_t repeats = tumo_ir_lab_repeats[app->repeat_index].count;
    bool ok = app->raw_count > 1;

    if(ok && furi_hal_infrared_is_busy()) {
        ok = false;
    }

    if(ok) {
        furi_hal_infrared_set_tx_output(output);
        for(uint8_t i = 0; i < repeats; i++) {
            infrared_send_raw_ext(
                app->raw_timings,
                app->raw_count,
                app->raw_start_from_mark,
                frequency,
                tumo_ir_lab_duty_value(app));
            furi_delay_ms(20);
        }
        furi_hal_infrared_set_tx_output(FuriHalInfraredTxPinInternal);
    }

    tumo_ir_lab_status_header(app, "RAW sample");
    furi_string_cat_printf(
        app->text,
        "Timings: %lu\nRepeat: %ux\nPin: %s\n\n%s",
        app->raw_count,
        repeats,
        tumo_ir_lab_pin_label(output),
        ok ? "RAW replay complete." : "RAW unavailable or IR is busy.");
    tumo_ir_lab_set_text(app);
}

static void tumo_ir_lab_run_protocol(TumoIrLabApp* app) {
    const TumoIrLabProtocolSample* sample = &tumo_ir_lab_protocols[app->protocol_index];
    const FuriHalInfraredTxPin output = tumo_ir_lab_resolve_output(app);
    bool ok = !furi_hal_infrared_is_busy();

    if(ok) {
        InfraredMessage message = {
            .protocol = sample->protocol,
            .address = sample->address,
            .command = sample->command,
            .repeat = false,
        };

        furi_hal_infrared_set_tx_output(output);
        infrared_send(&message, 1);
        furi_hal_infrared_set_tx_output(FuriHalInfraredTxPinInternal);
    }

    furi_string_printf(
        app->text,
        "Protocol sample\n\nProtocol: %s\nAddress: 0x%02lX\nCommand: 0x%02lX\nPin: %s\n\n%s",
        sample->label,
        sample->address,
        sample->command,
        tumo_ir_lab_pin_label(output),
        ok ? "Protocol TX complete." : "IR is busy. Close other IR apps.");
    tumo_ir_lab_set_text(app);
}

static void tumo_ir_lab_reset_raw(TumoIrLabApp* app) {
    app->raw_count = COUNT_OF(tumo_ir_lab_default_raw);
    app->raw_start_from_mark = true;
    memcpy(app->raw_timings, tumo_ir_lab_default_raw, sizeof(tumo_ir_lab_default_raw));
}

static bool tumo_ir_lab_save_preset(TumoIrLabApp* app) {
    storage_simply_mkdir(app->storage, TUMO_IR_LAB_DIR);

    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    bool success = false;

    do {
        if(!flipper_format_file_open_always(ff, TUMO_IR_LAB_PRESET_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, "Tumo IR Lab", 1)) break;

        uint32_t value = app->output_index;
        if(!flipper_format_write_uint32(ff, "OutputMode", &value, 1)) break;

        value = tumo_ir_lab_frequencies[app->frequency_index].hz;
        if(!flipper_format_write_uint32(ff, "Frequency", &value, 1)) break;

        value = tumo_ir_lab_duties[app->duty_index].percent;
        if(!flipper_format_write_uint32(ff, "DutyPercent", &value, 1)) break;

        value = tumo_ir_lab_bursts[app->burst_index].ms;
        if(!flipper_format_write_uint32(ff, "BurstMs", &value, 1)) break;

        value = tumo_ir_lab_repeats[app->repeat_index].count;
        if(!flipper_format_write_uint32(ff, "RawRepeat", &value, 1)) break;

        value = app->protocol_index;
        if(!flipper_format_write_uint32(ff, "ProtocolSample", &value, 1)) break;

        value = app->raw_start_from_mark ? 1U : 0U;
        if(!flipper_format_write_uint32(ff, "RawStartFromMark", &value, 1)) break;

        value = app->raw_count;
        if(!flipper_format_write_uint32(ff, "RawTimingsCount", &value, 1)) break;
        if(!flipper_format_write_uint32(ff, "RawTimings", app->raw_timings, app->raw_count)) {
            break;
        }

        success = true;
    } while(false);

    flipper_format_free(ff);
    return success;
}

static uint8_t tumo_ir_lab_find_frequency(uint32_t value) {
    for(size_t i = 0; i < COUNT_OF(tumo_ir_lab_frequencies); i++) {
        if(tumo_ir_lab_frequencies[i].hz == value) return i;
    }
    return 1;
}

static uint8_t tumo_ir_lab_find_duty(uint32_t value) {
    for(size_t i = 0; i < COUNT_OF(tumo_ir_lab_duties); i++) {
        if(tumo_ir_lab_duties[i].percent == value) return i;
    }
    return 1;
}

static uint8_t tumo_ir_lab_find_burst(uint32_t value) {
    for(size_t i = 0; i < COUNT_OF(tumo_ir_lab_bursts); i++) {
        if(tumo_ir_lab_bursts[i].ms == value) return i;
    }
    return 1;
}

static uint8_t tumo_ir_lab_find_repeat(uint32_t value) {
    for(size_t i = 0; i < COUNT_OF(tumo_ir_lab_repeats); i++) {
        if(tumo_ir_lab_repeats[i].count == value) return i;
    }
    return 0;
}

static bool tumo_ir_lab_load_preset(TumoIrLabApp* app) {
    const uint8_t old_output_index = app->output_index;
    const uint8_t old_frequency_index = app->frequency_index;
    const uint8_t old_duty_index = app->duty_index;
    const uint8_t old_burst_index = app->burst_index;
    const uint8_t old_repeat_index = app->repeat_index;
    const uint8_t old_protocol_index = app->protocol_index;
    const uint32_t old_raw_count = app->raw_count;
    const bool old_raw_start_from_mark = app->raw_start_from_mark;
    uint32_t old_raw_timings[TUMO_IR_LAB_RAW_MAX];
    memcpy(old_raw_timings, app->raw_timings, sizeof(old_raw_timings));

    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    FuriString* header = furi_string_alloc();
    uint32_t version = 0;
    uint32_t value = 0;
    bool success = false;

    do {
        if(!flipper_format_file_open_existing(ff, TUMO_IR_LAB_PRESET_PATH)) break;
        if(!flipper_format_read_header(ff, header, &version)) break;
        if(!furi_string_equal_str(header, "Tumo IR Lab") || (version != 1)) break;

        if(flipper_format_read_uint32(ff, "OutputMode", &value, 1)) {
            app->output_index = tumo_ir_lab_clamp_index(value, TumoIrLabOutputCount, 0);
        }

        if(flipper_format_read_uint32(ff, "Frequency", &value, 1)) {
            app->frequency_index = tumo_ir_lab_find_frequency(value);
        }

        if(flipper_format_read_uint32(ff, "DutyPercent", &value, 1)) {
            app->duty_index = tumo_ir_lab_find_duty(value);
        }

        if(flipper_format_read_uint32(ff, "BurstMs", &value, 1)) {
            app->burst_index = tumo_ir_lab_find_burst(value);
        }

        if(flipper_format_read_uint32(ff, "RawRepeat", &value, 1)) {
            app->repeat_index = tumo_ir_lab_find_repeat(value);
        }

        if(flipper_format_read_uint32(ff, "ProtocolSample", &value, 1)) {
            app->protocol_index =
                tumo_ir_lab_clamp_index(value, COUNT_OF(tumo_ir_lab_protocols), 0);
        }

        if(flipper_format_read_uint32(ff, "RawStartFromMark", &value, 1)) {
            app->raw_start_from_mark = value != 0;
        }

        if(!flipper_format_read_uint32(ff, "RawTimingsCount", &value, 1)) break;
        if((value < 2) || (value > TUMO_IR_LAB_RAW_MAX)) break;
        app->raw_count = value;
        if(!flipper_format_read_uint32(ff, "RawTimings", app->raw_timings, app->raw_count)) {
            break;
        }

        success = true;
    } while(false);

    furi_string_free(header);
    flipper_format_free(ff);

    if(!success) {
        app->output_index = old_output_index;
        app->frequency_index = old_frequency_index;
        app->duty_index = old_duty_index;
        app->burst_index = old_burst_index;
        app->repeat_index = old_repeat_index;
        app->protocol_index = old_protocol_index;
        app->raw_count = old_raw_count;
        app->raw_start_from_mark = old_raw_start_from_mark;
        memcpy(app->raw_timings, old_raw_timings, sizeof(old_raw_timings));
    }

    return success;
}

static void tumo_ir_lab_settings_refresh(TumoIrLabApp* app) {
    variable_item_set_current_value_index(app->output_item, app->output_index);
    variable_item_set_current_value_text(
        app->output_item, tumo_ir_lab_output_labels[app->output_index]);

    variable_item_set_current_value_index(app->frequency_item, app->frequency_index);
    variable_item_set_current_value_text(
        app->frequency_item, tumo_ir_lab_frequencies[app->frequency_index].label);

    variable_item_set_current_value_index(app->duty_item, app->duty_index);
    variable_item_set_current_value_text(
        app->duty_item, tumo_ir_lab_duties[app->duty_index].label);

    variable_item_set_current_value_index(app->burst_item, app->burst_index);
    variable_item_set_current_value_text(
        app->burst_item, tumo_ir_lab_bursts[app->burst_index].label);

    variable_item_set_current_value_index(app->repeat_item, app->repeat_index);
    variable_item_set_current_value_text(
        app->repeat_item, tumo_ir_lab_repeats[app->repeat_index].label);

    variable_item_set_current_value_index(app->protocol_item, app->protocol_index);
    variable_item_set_current_value_text(
        app->protocol_item, tumo_ir_lab_protocols[app->protocol_index].label);
}

static void tumo_ir_lab_output_changed(VariableItem* item) {
    TumoIrLabApp* app = variable_item_get_context(item);
    app->output_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(
        item, tumo_ir_lab_output_labels[app->output_index]);
}

static void tumo_ir_lab_frequency_changed(VariableItem* item) {
    TumoIrLabApp* app = variable_item_get_context(item);
    app->frequency_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(
        item, tumo_ir_lab_frequencies[app->frequency_index].label);
}

static void tumo_ir_lab_duty_changed(VariableItem* item) {
    TumoIrLabApp* app = variable_item_get_context(item);
    app->duty_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tumo_ir_lab_duties[app->duty_index].label);
}

static void tumo_ir_lab_burst_changed(VariableItem* item) {
    TumoIrLabApp* app = variable_item_get_context(item);
    app->burst_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tumo_ir_lab_bursts[app->burst_index].label);
}

static void tumo_ir_lab_repeat_changed(VariableItem* item) {
    TumoIrLabApp* app = variable_item_get_context(item);
    app->repeat_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, tumo_ir_lab_repeats[app->repeat_index].label);
}

static void tumo_ir_lab_protocol_changed(VariableItem* item) {
    TumoIrLabApp* app = variable_item_get_context(item);
    app->protocol_index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(
        item, tumo_ir_lab_protocols[app->protocol_index].label);
}

static void tumo_ir_lab_save(TumoIrLabApp* app) {
    const bool ok = tumo_ir_lab_save_preset(app);
    furi_string_printf(
        app->text,
        "Save preset\n\n%s\n\n%s",
        TUMO_IR_LAB_PRESET_PATH,
        ok ? "Preset saved." : "Failed to write preset.");
    tumo_ir_lab_set_text(app);
}

static void tumo_ir_lab_load(TumoIrLabApp* app) {
    const bool ok = tumo_ir_lab_load_preset(app);
    if(ok) {
        tumo_ir_lab_settings_refresh(app);
    }

    furi_string_printf(
        app->text,
        "Load preset\n\n%s\n\n%s",
        TUMO_IR_LAB_PRESET_PATH,
        ok ? "Preset loaded." : "Preset missing or invalid.");
    tumo_ir_lab_set_text(app);
}

static void tumo_ir_lab_show_about(TumoIrLabApp* app) {
    furi_string_set_str(
        app->text,
        "Tumo IR Lab 0.1\n\n"
        "Carrier burst, RAW sample replay, and NEC/SIRC/RC5 sample transmit.\n\n"
        "IR-only output. Module One uses PA7 external IR when selected or detected.");
    tumo_ir_lab_set_text(app);
}

static void tumo_ir_lab_menu_callback(void* context, uint32_t index) {
    TumoIrLabApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static bool tumo_ir_lab_custom_event_callback(void* context, uint32_t event) {
    TumoIrLabApp* app = context;

    switch(event) {
    case TumoIrLabMenuCarrier:
        tumo_ir_lab_run_carrier(app);
        return true;
    case TumoIrLabMenuRaw:
        tumo_ir_lab_run_raw(app);
        return true;
    case TumoIrLabMenuProtocol:
        tumo_ir_lab_run_protocol(app);
        return true;
    case TumoIrLabMenuSettings:
        view_dispatcher_switch_to_view(app->view_dispatcher, TumoIrLabViewSettings);
        return true;
    case TumoIrLabMenuSave:
        tumo_ir_lab_save(app);
        return true;
    case TumoIrLabMenuLoad:
        tumo_ir_lab_load(app);
        return true;
    case TumoIrLabMenuAbout:
        tumo_ir_lab_show_about(app);
        return true;
    default:
        return false;
    }
}

static uint32_t tumo_ir_lab_nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t tumo_ir_lab_nav_to_main(void* context) {
    UNUSED(context);
    return TumoIrLabViewMain;
}

static TumoIrLabApp* tumo_ir_lab_alloc(void) {
    TumoIrLabApp* app = malloc(sizeof(TumoIrLabApp));
    memset(app, 0, sizeof(TumoIrLabApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->text = furi_string_alloc();
    app->output_index = TumoIrLabOutputAuto;
    app->frequency_index = 1;
    app->duty_index = 1;
    app->burst_index = 1;
    app->repeat_index = 0;
    app->protocol_index = 0;
    tumo_ir_lab_reset_raw(app);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, tumo_ir_lab_custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "Tumo IR Lab");
    submenu_add_item(
        app->main_menu, "Carrier Burst", TumoIrLabMenuCarrier, tumo_ir_lab_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Replay RAW Sample", TumoIrLabMenuRaw, tumo_ir_lab_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Protocol Sample", TumoIrLabMenuProtocol, tumo_ir_lab_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Settings", TumoIrLabMenuSettings, tumo_ir_lab_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Save Preset", TumoIrLabMenuSave, tumo_ir_lab_menu_callback, app);
    submenu_add_item(
        app->main_menu, "Load Preset", TumoIrLabMenuLoad, tumo_ir_lab_menu_callback, app);
    submenu_add_item(
        app->main_menu, "About", TumoIrLabMenuAbout, tumo_ir_lab_menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->main_menu), tumo_ir_lab_nav_exit);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoIrLabViewMain, submenu_get_view(app->main_menu));

    app->settings = variable_item_list_alloc();
    app->output_item = variable_item_list_add(
        app->settings,
        "Output",
        TumoIrLabOutputCount,
        tumo_ir_lab_output_changed,
        app);
    app->frequency_item = variable_item_list_add(
        app->settings,
        "Carrier",
        COUNT_OF(tumo_ir_lab_frequencies),
        tumo_ir_lab_frequency_changed,
        app);
    app->duty_item = variable_item_list_add(
        app->settings, "Duty", COUNT_OF(tumo_ir_lab_duties), tumo_ir_lab_duty_changed, app);
    app->burst_item = variable_item_list_add(
        app->settings, "Burst", COUNT_OF(tumo_ir_lab_bursts), tumo_ir_lab_burst_changed, app);
    app->repeat_item = variable_item_list_add(
        app->settings,
        "RAW Repeat",
        COUNT_OF(tumo_ir_lab_repeats),
        tumo_ir_lab_repeat_changed,
        app);
    app->protocol_item = variable_item_list_add(
        app->settings,
        "Protocol",
        COUNT_OF(tumo_ir_lab_protocols),
        tumo_ir_lab_protocol_changed,
        app);
    tumo_ir_lab_settings_refresh(app);
    view_set_previous_callback(
        variable_item_list_get_view(app->settings), tumo_ir_lab_nav_to_main);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoIrLabViewSettings, variable_item_list_get_view(app->settings));

    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->text_box), tumo_ir_lab_nav_to_main);
    view_dispatcher_add_view(
        app->view_dispatcher, TumoIrLabViewText, text_box_get_view(app->text_box));

    view_dispatcher_switch_to_view(app->view_dispatcher, TumoIrLabViewMain);
    return app;
}

static void tumo_ir_lab_free(TumoIrLabApp* app) {
    furi_assert(app);

    if(!furi_hal_infrared_is_busy()) {
        furi_hal_infrared_set_tx_output(FuriHalInfraredTxPinInternal);
    }

    view_dispatcher_remove_view(app->view_dispatcher, TumoIrLabViewText);
    view_dispatcher_remove_view(app->view_dispatcher, TumoIrLabViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, TumoIrLabViewMain);
    text_box_free(app->text_box);
    variable_item_list_free(app->settings);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    furi_string_free(app->text);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumo_ir_lab_app(void* arg) {
    UNUSED(arg);

    TumoIrLabApp* app = tumo_ir_lab_alloc();
    view_dispatcher_run(app->view_dispatcher);
    tumo_ir_lab_free(app);
    return 0;
}
