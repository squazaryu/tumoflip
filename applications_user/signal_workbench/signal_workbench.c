#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIGNAL_WORKBENCH_DATA_DIR EXT_PATH("apps_data/signal_workbench")
#define SIGNAL_WORKBENCH_IR_DIR EXT_PATH("infrared")
#define SIGNAL_WORKBENCH_RF_NOTEBOOK_CSV \
    EXT_PATH("apps_data/arf_subghz_full/notebook/observations.csv")

#define SIGNAL_WORKBENCH_PATH_SIZE 128U
#define SIGNAL_WORKBENCH_IR_BUFFER_SIZE 8192U
#define SIGNAL_WORKBENCH_IR_TIMING_LIMIT 256U
#define SIGNAL_WORKBENCH_IR_PREVIEW_COUNT 8U
#define SIGNAL_WORKBENCH_LINE_SIZE 192U
#define SIGNAL_WORKBENCH_GPIO_CYCLES 8U
#define SIGNAL_WORKBENCH_GPIO_HIGH_US 500U
#define SIGNAL_WORKBENCH_GPIO_LOW_US 500U

typedef enum {
    SignalWorkbenchViewMenu,
    SignalWorkbenchViewText,
} SignalWorkbenchView;

typedef enum {
    SignalWorkbenchActionInspectIr,
    SignalWorkbenchActionRfNotebook,
    SignalWorkbenchActionGpioProfile,
    SignalWorkbenchActionRunGpioPulse,
    SignalWorkbenchActionExport,
    SignalWorkbenchActionAbout,
} SignalWorkbenchAction;

typedef struct {
    bool found;
    bool truncated;
    bool frequency_present;
    bool duty_present;
    bool type_present;
    char path[SIGNAL_WORKBENCH_PATH_SIZE];
    char type[16];
    uint64_t file_size;
    uint32_t frequency;
    float duty_cycle;
    uint32_t timing_count;
    uint32_t clipped_count;
    uint32_t preview[SIGNAL_WORKBENCH_IR_PREVIEW_COUNT];
    uint32_t preview_count;
    uint32_t min_us;
    uint32_t max_us;
    uint64_t total_us;
} SignalWorkbenchIrReport;

typedef struct {
    bool found;
    char source[16];
    char frequency_hz[16];
    char rssi_dbm[16];
    char radio[16];
    char note[48];
} SignalWorkbenchRfObservation;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    FuriString* text;
    SignalWorkbenchIrReport last_ir;
    SignalWorkbenchRfObservation last_rf;
    bool gpio_pulse_ran;
} SignalWorkbenchApp;

static const NotificationSequence signal_workbench_sequence_ok = {
    &message_display_backlight_on,
    &message_green_255,
    &message_delay_10,
    NULL,
};

static const NotificationSequence signal_workbench_sequence_error = {
    &message_display_backlight_on,
    &message_red_255,
    &message_delay_10,
    NULL,
};

static bool signal_workbench_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static void signal_workbench_format_filename_timestamp(char* output, size_t output_size) {
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

static void signal_workbench_append_iso_timestamp(FuriString* output) {
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

static bool signal_workbench_has_extension(const char* name, const char* extension) {
    const char* dot = strrchr(name, '.');
    return dot && strcmp(dot, extension) == 0;
}

static bool signal_workbench_find_latest_ir(
    Storage* storage,
    char* output,
    size_t output_size,
    uint64_t* file_size) {
    File* directory = storage_file_alloc(storage);
    FileInfo file_info;
    char name[96];
    char latest_name[96] = "";
    uint64_t latest_size = 0;
    bool found = false;

    if(storage_dir_open(directory, SIGNAL_WORKBENCH_IR_DIR)) {
        while(storage_dir_read(directory, &file_info, name, sizeof(name))) {
            if(file_info_is_dir(&file_info) || !signal_workbench_has_extension(name, ".ir")) {
                continue;
            }
            if(!found || strcmp(name, latest_name) > 0) {
                strlcpy(latest_name, name, sizeof(latest_name));
                latest_size = file_info.size;
                found = true;
            }
        }
        storage_dir_close(directory);
    }

    storage_file_free(directory);

    if(found) {
        snprintf(output, output_size, SIGNAL_WORKBENCH_IR_DIR "/%s", latest_name);
        if(file_size) {
            *file_size = latest_size;
        }
    }

    return found;
}

static size_t signal_workbench_read_file_bounded(
    Storage* storage,
    const char* path,
    char* buffer,
    size_t buffer_size,
    bool* truncated) {
    FileInfo info = {0};
    size_t bytes_read = 0;

    if(buffer_size == 0U) {
        return 0;
    }

    buffer[0] = '\0';
    if(truncated) {
        *truncated = false;
    }

    if(storage_common_stat(storage, path, &info) == FSE_OK && truncated) {
        *truncated = info.size >= buffer_size;
    }

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        bytes_read = storage_file_read(file, buffer, buffer_size - 1U);
        buffer[bytes_read] = '\0';
        if(truncated && bytes_read == (buffer_size - 1U)) {
            *truncated = true;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    return bytes_read;
}

static const char* signal_workbench_skip_spaces(const char* cursor) {
    while((*cursor == ' ') || (*cursor == '\t')) {
        cursor++;
    }
    return cursor;
}

static bool signal_workbench_parse_token(
    const char* buffer,
    const char* key,
    char* output,
    size_t output_size) {
    const char* cursor = strstr(buffer, key);
    if(!cursor) {
        return false;
    }

    cursor += strlen(key);
    cursor = signal_workbench_skip_spaces(cursor);

    size_t index = 0;
    while(*cursor && (*cursor != '\n') && (*cursor != '\r') && (index + 1U < output_size)) {
        output[index++] = *cursor++;
    }
    output[index] = '\0';
    return index > 0U;
}

static bool signal_workbench_parse_u32(
    const char* buffer,
    const char* key,
    uint32_t* output) {
    char token[24];
    if(!signal_workbench_parse_token(buffer, key, token, sizeof(token))) {
        return false;
    }

    char* end = NULL;
    const unsigned long parsed = strtoul(token, &end, 10);
    if(end == token) {
        return false;
    }

    *output = (uint32_t)parsed;
    return true;
}

static bool signal_workbench_parse_float(
    const char* buffer,
    const char* key,
    float* output) {
    char token[24];
    if(!signal_workbench_parse_token(buffer, key, token, sizeof(token))) {
        return false;
    }

    char* end = NULL;
    const float parsed = strtof(token, &end);
    if(end == token) {
        return false;
    }

    *output = parsed;
    return true;
}

static void signal_workbench_add_timing(SignalWorkbenchIrReport* report, uint32_t timing) {
    if(report->timing_count >= SIGNAL_WORKBENCH_IR_TIMING_LIMIT) {
        report->clipped_count++;
        return;
    }

    if(report->preview_count < SIGNAL_WORKBENCH_IR_PREVIEW_COUNT) {
        report->preview[report->preview_count++] = timing;
    }

    if(report->timing_count == 0U || timing < report->min_us) {
        report->min_us = timing;
    }
    if(timing > report->max_us) {
        report->max_us = timing;
    }

    report->total_us += timing;
    report->timing_count++;
}

static void signal_workbench_parse_timings_from_key(
    const char* buffer,
    const char* key,
    SignalWorkbenchIrReport* report) {
    const char* cursor = strstr(buffer, key);
    if(!cursor) {
        return;
    }

    cursor += strlen(key);
    while(*cursor && (*cursor != '\n') && (*cursor != '\r')) {
        cursor = signal_workbench_skip_spaces(cursor);
        while(*cursor == ',') {
            cursor++;
            cursor = signal_workbench_skip_spaces(cursor);
        }

        if(!*cursor || (*cursor == '\n') || (*cursor == '\r')) {
            break;
        }

        char* end = NULL;
        long parsed = strtol(cursor, &end, 10);
        if(end == cursor) {
            break;
        }

        if(parsed < 0) {
            parsed = -parsed;
        }
        signal_workbench_add_timing(report, (uint32_t)parsed);
        cursor = end;
    }
}

static void signal_workbench_analyze_ir(SignalWorkbenchApp* app, SignalWorkbenchIrReport* report) {
    memset(report, 0, sizeof(SignalWorkbenchIrReport));

    uint64_t file_size = 0;
    report->found = signal_workbench_find_latest_ir(
        app->storage, report->path, sizeof(report->path), &file_size);
    report->file_size = file_size;
    if(!report->found) {
        return;
    }

    char* buffer = malloc(SIGNAL_WORKBENCH_IR_BUFFER_SIZE);
    if(!buffer) {
        report->truncated = true;
        return;
    }

    signal_workbench_read_file_bounded(
        app->storage,
        report->path,
        buffer,
        SIGNAL_WORKBENCH_IR_BUFFER_SIZE,
        &report->truncated);

    report->type_present =
        signal_workbench_parse_token(buffer, "type:", report->type, sizeof(report->type));
    report->frequency_present =
        signal_workbench_parse_u32(buffer, "frequency:", &report->frequency);
    report->duty_present = signal_workbench_parse_float(buffer, "duty_cycle:", &report->duty_cycle);
    signal_workbench_parse_timings_from_key(buffer, "data:", report);
    signal_workbench_parse_timings_from_key(buffer, "RAW_Data:", report);

    free(buffer);
}

static void signal_workbench_csv_read_field(const char** cursor, char* output, size_t output_size) {
    size_t index = 0;
    while(**cursor && (**cursor != ',') && (**cursor != '\n') && (**cursor != '\r')) {
        if(index + 1U < output_size) {
            output[index++] = **cursor;
        }
        (*cursor)++;
    }
    output[index] = '\0';
    if(**cursor == ',') {
        (*cursor)++;
    }
}

static bool signal_workbench_parse_rf_line(
    const char* line,
    SignalWorkbenchRfObservation* observation) {
    memset(observation, 0, sizeof(SignalWorkbenchRfObservation));
    if(!line || !line[0] || strncmp(line, "timestamp,", 10) == 0) {
        return false;
    }

    char ignored[32];
    const char* cursor = line;
    signal_workbench_csv_read_field(&cursor, ignored, sizeof(ignored));
    signal_workbench_csv_read_field(&cursor, observation->source, sizeof(observation->source));
    signal_workbench_csv_read_field(
        &cursor, observation->frequency_hz, sizeof(observation->frequency_hz));
    signal_workbench_csv_read_field(&cursor, observation->rssi_dbm, sizeof(observation->rssi_dbm));
    signal_workbench_csv_read_field(&cursor, ignored, sizeof(ignored));
    signal_workbench_csv_read_field(&cursor, ignored, sizeof(ignored));
    signal_workbench_csv_read_field(&cursor, observation->radio, sizeof(observation->radio));
    signal_workbench_csv_read_field(&cursor, ignored, sizeof(ignored));
    signal_workbench_csv_read_field(&cursor, ignored, sizeof(ignored));
    signal_workbench_csv_read_field(&cursor, ignored, sizeof(ignored));
    signal_workbench_csv_read_field(&cursor, ignored, sizeof(ignored));
    signal_workbench_csv_read_field(&cursor, observation->note, sizeof(observation->note));

    observation->found = observation->frequency_hz[0] != '\0';
    return observation->found;
}

static bool signal_workbench_read_latest_rf_observation(
    SignalWorkbenchApp* app,
    SignalWorkbenchRfObservation* observation) {
    File* file = storage_file_alloc(app->storage);
    bool found = false;
    char current[SIGNAL_WORKBENCH_LINE_SIZE];
    char latest[SIGNAL_WORKBENCH_LINE_SIZE];
    size_t current_len = 0;
    current[0] = '\0';
    latest[0] = '\0';

    if(storage_file_open(file, SIGNAL_WORKBENCH_RF_NOTEBOOK_CSV, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char ch;
        while(storage_file_read(file, &ch, 1) == 1) {
            if((ch == '\n') || (ch == '\r')) {
                current[current_len] = '\0';
                if(current_len > 0U && strncmp(current, "timestamp,", 10) != 0) {
                    strlcpy(latest, current, sizeof(latest));
                    found = true;
                }
                current_len = 0;
                current[0] = '\0';
            } else if(current_len + 1U < sizeof(current)) {
                current[current_len++] = ch;
            }
        }
        if(current_len > 0U) {
            current[current_len] = '\0';
            if(strncmp(current, "timestamp,", 10) != 0) {
                strlcpy(latest, current, sizeof(latest));
                found = true;
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    return found && signal_workbench_parse_rf_line(latest, observation);
}

static bool signal_workbench_write_text_file(
    Storage* storage,
    const char* path,
    const char* text) {
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        const size_t size = strlen(text);
        ok = storage_file_write(file, text, size) == size;
        storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

static void signal_workbench_append_ir_report(
    FuriString* output,
    const SignalWorkbenchIrReport* report) {
    furi_string_cat(output, "IR RAW\n");
    if(!report->found) {
        furi_string_cat_printf(
            output,
            "Status: missing\n"
            "Path scanned: %s\n\n",
            SIGNAL_WORKBENCH_IR_DIR);
        return;
    }

    furi_string_cat_printf(
        output,
        "Path: %s\n"
        "File size: %lu bytes\n"
        "Read limit: %u bytes\n"
        "Truncated: %s\n"
        "Type: %s\n"
        "Frequency: %s%lu Hz\n"
        "Duty: %s%u.%02u\n"
        "Timings counted: %lu\n"
        "Timings clipped: %lu\n",
        report->path,
        (unsigned long)report->file_size,
        SIGNAL_WORKBENCH_IR_BUFFER_SIZE,
        report->truncated ? "yes" : "no",
        report->type_present ? report->type : "unknown",
        report->frequency_present ? "" : "missing/",
        (unsigned long)report->frequency,
        report->duty_present ? "" : "missing/",
        (unsigned int)report->duty_cycle,
        (unsigned int)((report->duty_cycle - (float)((unsigned int)report->duty_cycle)) * 100.0f),
        (unsigned long)report->timing_count,
        (unsigned long)report->clipped_count);

    if(report->timing_count > 0U) {
        const uint32_t avg_us = (uint32_t)(report->total_us / report->timing_count);
        furi_string_cat_printf(
            output,
            "Min/Avg/Max: %lu/%lu/%lu us\n"
            "Total preview duration: %lu us\n"
            "Preview:",
            (unsigned long)report->min_us,
            (unsigned long)avg_us,
            (unsigned long)report->max_us,
            (unsigned long)report->total_us);
        for(uint32_t i = 0; i < report->preview_count; i++) {
            furi_string_cat_printf(output, " %lu", (unsigned long)report->preview[i]);
        }
        furi_string_cat(output, "\n\n");
    } else {
        furi_string_cat(output, "Timings: none found in bounded window\n\n");
    }
}

static void signal_workbench_append_rf_report(
    FuriString* output,
    const SignalWorkbenchRfObservation* observation) {
    furi_string_cat(output, "Receive-only RF Context\n");
    furi_string_cat_printf(output, "Source: %s\n", SIGNAL_WORKBENCH_RF_NOTEBOOK_CSV);
    if(!observation->found) {
        furi_string_cat(output, "Status: no ARF Frequency Analyzer notebook row\n\n");
        return;
    }

    furi_string_cat_printf(
        output,
        "Last row: %s\n"
        "Frequency: %s Hz\n"
        "RSSI: %s dBm\n"
        "Radio: %s\n"
        "Note: %s\n\n",
        observation->source,
        observation->frequency_hz,
        observation->rssi_dbm,
        observation->radio,
        observation->note);
}

static void signal_workbench_append_gpio_profile(FuriString* output, bool ran) {
    const uint32_t period_us = SIGNAL_WORKBENCH_GPIO_HIGH_US + SIGNAL_WORKBENCH_GPIO_LOW_US;
    const uint32_t total_us = period_us * SIGNAL_WORKBENCH_GPIO_CYCLES;
    furi_string_cat_printf(
        output,
        "GPIO Pulse Profile\n"
        "Pin: gpio_ext_pc0\n"
        "Frequency: 1000 Hz\n"
        "Cycles: %u\n"
        "High/Low: %u/%u us\n"
        "Total: %lu us\n"
        "Bounded: yes\n"
        "Restores: GpioModeAnalog\n"
        "Last run: %s\n\n",
        SIGNAL_WORKBENCH_GPIO_CYCLES,
        SIGNAL_WORKBENCH_GPIO_HIGH_US,
        SIGNAL_WORKBENCH_GPIO_LOW_US,
        (unsigned long)total_us,
        ran ? "yes" : "no");
}

static void signal_workbench_build_report(SignalWorkbenchApp* app, FuriString* output) {
    furi_string_reset(output);
    furi_string_cat(output, "Signal Workbench\n");
    furi_string_cat(output, "Generated: ");
    signal_workbench_append_iso_timestamp(output);
    furi_string_cat(output, "\n\n");
    signal_workbench_append_ir_report(output, &app->last_ir);
    signal_workbench_append_rf_report(output, &app->last_rf);
    signal_workbench_append_gpio_profile(output, app->gpio_pulse_ran);
    furi_string_cat(
        output,
        "Safety\n"
        "Sub-GHz is receive-only metadata imported from the ARF notebook.\n"
        "This app does not own CC1101 and does not transmit Sub-GHz.\n");
}

static void signal_workbench_show_text(SignalWorkbenchApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->view_dispatcher, SignalWorkbenchViewText);
}

static void signal_workbench_run_ir_inspect(SignalWorkbenchApp* app) {
    signal_workbench_analyze_ir(app, &app->last_ir);
    furi_string_reset(app->text);
    furi_string_cat(app->text, "IR Inspect\n\n");
    signal_workbench_append_ir_report(app->text, &app->last_ir);
    notification_message(
        app->notification,
        app->last_ir.found ? &signal_workbench_sequence_ok : &signal_workbench_sequence_error);
}

static void signal_workbench_run_rf_notebook(SignalWorkbenchApp* app) {
    memset(&app->last_rf, 0, sizeof(app->last_rf));
    signal_workbench_read_latest_rf_observation(app, &app->last_rf);
    furi_string_reset(app->text);
    signal_workbench_append_rf_report(app->text, &app->last_rf);
}

static void signal_workbench_build_gpio_profile(SignalWorkbenchApp* app) {
    furi_string_reset(app->text);
    signal_workbench_append_gpio_profile(app->text, app->gpio_pulse_ran);
}

static void signal_workbench_run_gpio_pulse(SignalWorkbenchApp* app) {
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeOutputPushPull);
    for(uint32_t i = 0; i < SIGNAL_WORKBENCH_GPIO_CYCLES; i++) {
        furi_hal_gpio_write(&gpio_ext_pc0, true);
        furi_delay_us(SIGNAL_WORKBENCH_GPIO_HIGH_US);
        furi_hal_gpio_write(&gpio_ext_pc0, false);
        furi_delay_us(SIGNAL_WORKBENCH_GPIO_LOW_US);
    }
    furi_hal_gpio_write(&gpio_ext_pc0, false);
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog);

    app->gpio_pulse_ran = true;
    furi_string_reset(app->text);
    furi_string_cat(app->text, "GPIO Pulse\n\n");
    signal_workbench_append_gpio_profile(app->text, true);
    notification_message(app->notification, &signal_workbench_sequence_ok);
}

static void signal_workbench_export_report(SignalWorkbenchApp* app) {
    signal_workbench_analyze_ir(app, &app->last_ir);
    memset(&app->last_rf, 0, sizeof(app->last_rf));
    signal_workbench_read_latest_rf_observation(app, &app->last_rf);

    FuriString* report = furi_string_alloc();
    signal_workbench_build_report(app, report);

    char timestamp[32];
    char path[SIGNAL_WORKBENCH_PATH_SIZE];
    signal_workbench_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(path, sizeof(path), SIGNAL_WORKBENCH_DATA_DIR "/signal_%s.txt", timestamp);

    const bool dir_ok = signal_workbench_mkdir(app->storage, SIGNAL_WORKBENCH_DATA_DIR);
    const bool write_ok =
        dir_ok && signal_workbench_write_text_file(app->storage, path, furi_string_get_cstr(report));

    furi_string_reset(app->text);
    furi_string_cat_printf(
        app->text,
        "Signal Workbench Export\n\n"
        "Result: %s\n"
        "Path:\n%s\n\n",
        write_ok ? "saved" : (dir_ok ? "write failed" : "mkdir failed"),
        path);
    furi_string_cat(app->text, report);
    notification_message(
        app->notification, write_ok ? &signal_workbench_sequence_ok : &signal_workbench_sequence_error);
    furi_string_free(report);
}

static void signal_workbench_build_about(SignalWorkbenchApp* app) {
    furi_string_reset(app->text);
    furi_string_cat(
        app->text,
        "Signal Workbench\n\n"
        "Bounded workspace for saved signal inspection.\n\n"
        "IR: reads the latest file in /ext/infrared and summarizes RAW timing metadata.\n"
        "RF: imports the latest receive-only ARF Frequency Analyzer notebook row.\n"
        "GPIO: explicit short PC0 pulse profile only.");
}

static void signal_workbench_menu_callback(void* context, uint32_t index) {
    SignalWorkbenchApp* app = context;

    switch(index) {
    case SignalWorkbenchActionInspectIr:
        signal_workbench_run_ir_inspect(app);
        break;
    case SignalWorkbenchActionRfNotebook:
        signal_workbench_run_rf_notebook(app);
        break;
    case SignalWorkbenchActionGpioProfile:
        signal_workbench_build_gpio_profile(app);
        break;
    case SignalWorkbenchActionRunGpioPulse:
        signal_workbench_run_gpio_pulse(app);
        break;
    case SignalWorkbenchActionExport:
        signal_workbench_export_report(app);
        break;
    case SignalWorkbenchActionAbout:
        signal_workbench_build_about(app);
        break;
    default:
        furi_string_reset(app->text);
        furi_string_cat(app->text, "Unsupported action.\n");
        break;
    }

    signal_workbench_show_text(app);
}

static bool signal_workbench_back_callback(void* context) {
    view_dispatcher_stop(((SignalWorkbenchApp*)context)->view_dispatcher);
    return true;
}

static uint32_t signal_workbench_text_previous_callback(void* context) {
    UNUSED(context);
    return SignalWorkbenchViewMenu;
}

static SignalWorkbenchApp* signal_workbench_alloc(void) {
    SignalWorkbenchApp* app = malloc(sizeof(SignalWorkbenchApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_box = text_box_alloc();
    app->text = furi_string_alloc();
    memset(&app->last_ir, 0, sizeof(app->last_ir));
    memset(&app->last_rf, 0, sizeof(app->last_rf));
    app->gpio_pulse_ran = false;

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, signal_workbench_back_callback);

    submenu_set_header(app->submenu, "Signal Workbench");
    submenu_add_item(
        app->submenu,
        "IR: Inspect Latest",
        SignalWorkbenchActionInspectIr,
        signal_workbench_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "RF: Notebook Row",
        SignalWorkbenchActionRfNotebook,
        signal_workbench_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "GPIO: Profile",
        SignalWorkbenchActionGpioProfile,
        signal_workbench_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "GPIO: Run Pulse PC0",
        SignalWorkbenchActionRunGpioPulse,
        signal_workbench_menu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Export Report",
        SignalWorkbenchActionExport,
        signal_workbench_menu_callback,
        app);
    submenu_add_item(
        app->submenu, "About", SignalWorkbenchActionAbout, signal_workbench_menu_callback, app);

    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_previous_callback(
        text_box_get_view(app->text_box), signal_workbench_text_previous_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, SignalWorkbenchViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, SignalWorkbenchViewText, text_box_get_view(app->text_box));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, SignalWorkbenchViewMenu);
    return app;
}

static void signal_workbench_free(SignalWorkbenchApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, SignalWorkbenchViewText);
    view_dispatcher_remove_view(app->view_dispatcher, SignalWorkbenchViewMenu);
    text_box_free(app->text_box);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->text);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t signal_workbench_app(void* context) {
    UNUSED(context);
    SignalWorkbenchApp* app = signal_workbench_alloc();
    view_dispatcher_run(app->view_dispatcher);
    signal_workbench_free(app);
    return 0;
}
