#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_infrared.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUMOSCRIPT_APP_ID "tumoscript"
#define TUMOSCRIPT_DATA_DIR EXT_PATH("apps_data/tumoscript")
#define TUMOSCRIPT_SCRIPTS_DIR TUMOSCRIPT_DATA_DIR "/scripts"
#define TUMOSCRIPT_RUNS_DIR TUMOSCRIPT_DATA_DIR "/runs"
#define TUMOSCRIPT_SAMPLE_PATH TUMOSCRIPT_SCRIPTS_DIR "/safe_demo.tscr"

#define TUMOSCRIPT_MAX_FILES 12U
#define TUMOSCRIPT_MAX_STEPS 32U
#define TUMOSCRIPT_MAX_LABELS 16U
#define TUMOSCRIPT_MAX_EXECUTED_STEPS 64U
#define TUMOSCRIPT_MAX_FILE_BYTES 8192U
#define TUMOSCRIPT_QUEUE_DEPTH 8U
#define TUMOSCRIPT_NAME_SIZE 48U
#define TUMOSCRIPT_PATH_SIZE 160U
#define TUMOSCRIPT_LINE_SIZE 192U
#define TUMOSCRIPT_VALUE_SIZE 112U
#define TUMOSCRIPT_TOKEN_SIZE 32U
#define TUMOSCRIPT_STATUS_SIZE 64U
#define TUMOSCRIPT_DELAY_CHUNK_MS 100U
#define TUMOSCRIPT_MAX_DELAY_MS 600000UL
#define TUMOSCRIPT_GPIO_MAX_CYCLES 64U
#define TUMOSCRIPT_GPIO_MIN_PULSE_US 50U
#define TUMOSCRIPT_GPIO_MAX_PULSE_US 500000U
#define TUMOSCRIPT_GPIO_MAX_TOTAL_US 1000000U
#define TUMOSCRIPT_IR_MIN_FREQUENCY 10000U
#define TUMOSCRIPT_IR_MAX_FREQUENCY 100000U
#define TUMOSCRIPT_IR_MIN_DURATION_MS 10U
#define TUMOSCRIPT_IR_MAX_DURATION_MS 500U

typedef enum {
    TumoScriptModeBrowse,
    TumoScriptModeRun,
} TumoScriptMode;

typedef enum {
    TumoScriptRunModeValidate,
    TumoScriptRunModeDryRun,
    TumoScriptRunModeExecute,
} TumoScriptRunMode;

typedef enum {
    TumoScriptStepLabel,
    TumoScriptStepLog,
    TumoScriptStepDelay,
    TumoScriptStepPrompt,
    TumoScriptStepBridge,
    TumoScriptStepGpioPulse,
    TumoScriptStepIrBurst,
    TumoScriptStepBranchOk,
    TumoScriptStepBranchError,
    TumoScriptStepGoto,
} TumoScriptStepType;

typedef struct {
    char name[TUMOSCRIPT_NAME_SIZE];
    char path[TUMOSCRIPT_PATH_SIZE];
} TumoScriptFile;

typedef struct {
    char name[TUMOSCRIPT_TOKEN_SIZE];
    uint8_t step_index;
    uint32_t line_no;
} TumoScriptLabel;

typedef struct {
    TumoScriptStepType type;
    uint32_t line_no;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    char arg[TUMOSCRIPT_TOKEN_SIZE];
    char value[TUMOSCRIPT_VALUE_SIZE];
} TumoScriptStep;

typedef struct {
    char name[TUMOSCRIPT_NAME_SIZE];
    char path[TUMOSCRIPT_PATH_SIZE];
    TumoScriptStep steps[TUMOSCRIPT_MAX_STEPS];
    TumoScriptLabel labels[TUMOSCRIPT_MAX_LABELS];
    uint8_t step_count;
    uint8_t label_count;
    uint8_t parse_errors;
    uint8_t validation_errors;
    bool continue_on_error;
    char first_error[TUMOSCRIPT_STATUS_SIZE];
} TumoScriptPlan;

typedef struct {
    uint32_t remaining_us;
} TumoScriptIrTx;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Storage* storage;
    Bt* bt;
    NotificationApp* notification;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    File* log_file;
    TumoScriptFile scripts[TUMOSCRIPT_MAX_FILES];
    uint8_t script_count;
    uint8_t selected;
    uint8_t current_step;
    uint8_t total_steps;
    uint32_t request_id_next;
    TumoScriptMode mode;
    bool cancel_requested;
    char status[TUMOSCRIPT_STATUS_SIZE];
    char detail[TUMOSCRIPT_STATUS_SIZE];
    char log_path[TUMOSCRIPT_PATH_SIZE];
} TumoScriptApp;

static const NotificationSequence tumoscript_sequence_ok = {
    &message_display_backlight_on,
    &message_green_255,
    &message_delay_10,
    NULL,
};

static const NotificationSequence tumoscript_sequence_error = {
    &message_display_backlight_on,
    &message_red_255,
    &message_delay_10,
    NULL,
};

static const char tumoscript_sample_script[] =
    "# TumoScript v1 safe_demo.tscr\n"
    "name Safe Demo\n"
    "policy stop\n"
    "log Scenario started\n"
    "delay 250\n"
    "prompt Press OK to continue\n"
    "bridge event safe_demo_started\n"
    "gpio_pulse pc0 4 500 500\n"
    "label done\n"
    "log Scenario done\n";

static const char* tumoscript_step_name(TumoScriptStepType type);

static bool tumoscript_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static bool tumoscript_write(File* file, const char* text) {
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size;
}

static void tumoscript_format_iso_timestamp(char* output, size_t output_size) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    snprintf(
        output,
        output_size,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
}

static void tumoscript_format_filename_timestamp(char* output, size_t output_size) {
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

static void tumoscript_set_model(
    TumoScriptApp* app,
    TumoScriptMode mode,
    const char* status,
    const char* detail,
    uint8_t current_step,
    uint8_t total_steps) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->mode = mode;
    app->current_step = current_step;
    app->total_steps = total_steps;
    strlcpy(app->status, status ? status : "", sizeof(app->status));
    strlcpy(app->detail, detail ? detail : "", sizeof(app->detail));
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    view_port_update(app->view_port);
}

static void
    tumoscript_copy_draw_text(char* output, size_t output_size, const char* input, size_t max_chars) {
    if(output_size == 0U) return;
    size_t copied = 0;
    while(input[copied] && copied < max_chars && (copied + 1U) < output_size) {
        output[copied] = input[copied];
        copied++;
    }
    if(input[copied] && copied > 0U && (copied + 1U) < output_size) {
        output[copied - 1U] = '~';
    }
    output[copied] = '\0';
}

static void tumoscript_draw_callback(Canvas* canvas, void* context) {
    TumoScriptApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    char script_name[27];
    char status[27];
    char detail[27];
    const bool has_scripts = app->script_count > 0U;
    const uint8_t selected = app->selected;
    tumoscript_copy_draw_text(
        script_name,
        sizeof(script_name),
        has_scripts ? app->scripts[selected].name : "No .tscr files",
        25U);
    tumoscript_copy_draw_text(status, sizeof(status), app->status, 25U);
    tumoscript_copy_draw_text(detail, sizeof(detail), app->detail, 25U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "TumoScript");
    canvas_set_font(canvas, FontSecondary);

    if(app->mode == TumoScriptModeBrowse) {
        char counter[30];
        snprintf(
            counter,
            sizeof(counter),
            "Script %u/%u",
            has_scripts ? (unsigned)(selected + 1U) : 0U,
            (unsigned)app->script_count);
        canvas_draw_str(canvas, 0, 23, counter);
        canvas_draw_str(canvas, 0, 35, script_name);
        canvas_draw_str(canvas, 0, 48, status);
        canvas_draw_str(canvas, 0, 60, detail);
        canvas_draw_str_aligned(canvas, 127, 12, AlignRight, AlignBottom, "OK Run");
        canvas_draw_str_aligned(canvas, 127, 24, AlignRight, AlignBottom, "< Validate");
        canvas_draw_str_aligned(canvas, 127, 36, AlignRight, AlignBottom, "> Dry");
    } else {
        char progress[30];
        snprintf(
            progress,
            sizeof(progress),
            "Step %u/%u",
            (unsigned)app->current_step,
            (unsigned)app->total_steps);
        canvas_draw_str(canvas, 0, 23, progress);
        canvas_draw_str(canvas, 0, 35, script_name);
        canvas_draw_str(canvas, 0, 48, status);
        canvas_draw_str(canvas, 0, 60, detail);
        canvas_draw_str_aligned(canvas, 127, 12, AlignRight, AlignBottom, "Back Cancel");
    }

    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void tumoscript_input_callback(InputEvent* input_event, void* context) {
    TumoScriptApp* app = context;
    furi_message_queue_put(app->queue, input_event, 0);
}

static bool tumoscript_name_ends_with(const char* name, const char* suffix) {
    const size_t name_len = strlen(name);
    const size_t suffix_len = strlen(suffix);
    return (name_len >= suffix_len) && (strcmp(name + name_len - suffix_len, suffix) == 0);
}

static void tumoscript_ensure_dirs(TumoScriptApp* app) {
    tumoscript_mkdir(app->storage, EXT_PATH("apps_data"));
    tumoscript_mkdir(app->storage, TUMOSCRIPT_DATA_DIR);
    tumoscript_mkdir(app->storage, TUMOSCRIPT_SCRIPTS_DIR);
    tumoscript_mkdir(app->storage, TUMOSCRIPT_RUNS_DIR);
}

static void tumoscript_ensure_sample(TumoScriptApp* app) {
    if(storage_file_exists(app->storage, TUMOSCRIPT_SAMPLE_PATH)) return;

    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, TUMOSCRIPT_SAMPLE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        tumoscript_write(file, tumoscript_sample_script);
        storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void tumoscript_scan_scripts(TumoScriptApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->script_count = 0;
    app->selected = 0;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    File* directory = storage_file_alloc(app->storage);
    if(storage_dir_open(directory, TUMOSCRIPT_SCRIPTS_DIR)) {
        FileInfo file_info;
        char name[64];
        while(storage_dir_read(directory, &file_info, name, sizeof(name))) {
            if(file_info_is_dir(&file_info) || !tumoscript_name_ends_with(name, ".tscr")) {
                continue;
            }

            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->script_count < TUMOSCRIPT_MAX_FILES) {
                TumoScriptFile* script = &app->scripts[app->script_count++];
                strlcpy(script->name, name, sizeof(script->name));
                snprintf(script->path, sizeof(script->path), TUMOSCRIPT_SCRIPTS_DIR "/%s", name);
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    for(uint8_t i = 1; i < app->script_count; i++) {
        TumoScriptFile current = app->scripts[i];
        uint8_t j = i;
        while((j > 0U) && (strcmp(app->scripts[j - 1U].name, current.name) > 0)) {
            app->scripts[j] = app->scripts[j - 1U];
            j--;
        }
        app->scripts[j] = current;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static char* tumoscript_trim(char* text) {
    while(isspace((unsigned char)*text)) {
        text++;
    }

    char* end = text + strlen(text);
    while((end > text) && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool tumoscript_starts_with_key(const char* text, const char* key) {
    while(*key) {
        if(tolower((unsigned char)*text) != tolower((unsigned char)*key)) {
            return false;
        }
        text++;
        key++;
    }
    return true;
}

static const char* tumoscript_value_after_key(char* text, const char* key) {
    const size_t key_len = strlen(key);
    if(!tumoscript_starts_with_key(text, key)) return NULL;

    char separator = text[key_len];
    if(separator == '\0') return text + key_len;
    if((separator != ':') && (separator != '=') && !isspace((unsigned char)separator)) return NULL;

    char* value = text + key_len;
    if((*value == ':') || (*value == '=')) value++;
    return tumoscript_trim(value);
}

static void tumoscript_set_first_error(TumoScriptPlan* plan, uint32_t line_no, const char* error) {
    if(plan->first_error[0] == '\0') {
        snprintf(
            plan->first_error,
            sizeof(plan->first_error),
            "line %lu: %s",
            (unsigned long)line_no,
            error);
    }
}

static bool tumoscript_parse_u32(const char** cursor, uint32_t* value) {
    const char* text = *cursor;
    while(isspace((unsigned char)*text)) {
        text++;
    }

    uint32_t parsed = 0;
    bool has_digit = false;
    while(isdigit((unsigned char)*text)) {
        has_digit = true;
        parsed = (parsed * 10U) + (uint32_t)(*text - '0');
        text++;
    }
    if(!has_digit) return false;

    *value = parsed;
    *cursor = text;
    return true;
}

static bool tumoscript_parse_word(const char** cursor, char* output, size_t output_size) {
    const char* text = *cursor;
    while(isspace((unsigned char)*text)) {
        text++;
    }

    size_t index = 0;
    while(*text && !isspace((unsigned char)*text)) {
        if(index + 1U < output_size) {
            output[index++] = *text;
        }
        text++;
    }
    output[index] = '\0';
    *cursor = text;
    return index > 0U;
}

static const char* tumoscript_rest(const char* cursor) {
    while(isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

static int16_t tumoscript_find_label(const TumoScriptPlan* plan, const char* name) {
    for(uint8_t i = 0; i < plan->label_count; i++) {
        if(strcmp(plan->labels[i].name, name) == 0) {
            return plan->labels[i].step_index;
        }
    }
    return -1;
}

static bool tumoscript_add_step(
    TumoScriptPlan* plan,
    TumoScriptStepType type,
    uint32_t line_no,
    const char* arg,
    const char* value,
    uint32_t a,
    uint32_t b,
    uint32_t c) {
    if(plan->step_count >= TUMOSCRIPT_MAX_STEPS) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "too many steps");
        return false;
    }

    TumoScriptStep* step = &plan->steps[plan->step_count++];
    memset(step, 0, sizeof(TumoScriptStep));
    step->type = type;
    step->line_no = line_no;
    step->a = a;
    step->b = b;
    step->c = c;
    strlcpy(step->arg, arg ? arg : "", sizeof(step->arg));
    strlcpy(step->value, value ? value : "", sizeof(step->value));
    return true;
}

static bool tumoscript_add_label(TumoScriptPlan* plan, const char* name, uint32_t line_no) {
    if(!name || !name[0]) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "empty label");
        return false;
    }
    if(tumoscript_find_label(plan, name) >= 0) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "duplicate label");
        return false;
    }
    if(plan->label_count >= TUMOSCRIPT_MAX_LABELS) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "too many labels");
        return false;
    }

    TumoScriptLabel* label = &plan->labels[plan->label_count++];
    strlcpy(label->name, name, sizeof(label->name));
    label->step_index = plan->step_count;
    label->line_no = line_no;
    return tumoscript_add_step(plan, TumoScriptStepLabel, line_no, name, name, 0, 0, 0);
}

static void tumoscript_parse_gpio_pulse(TumoScriptPlan* plan, const char* value, uint32_t line_no) {
    const char* cursor = value;
    char pin[TUMOSCRIPT_TOKEN_SIZE];
    uint32_t cycles = 0;
    uint32_t high_us = 0;
    uint32_t low_us = 0;

    if(!tumoscript_parse_word(&cursor, pin, sizeof(pin)) ||
       !tumoscript_parse_u32(&cursor, &cycles) ||
       !tumoscript_parse_u32(&cursor, &high_us) ||
       !tumoscript_parse_u32(&cursor, &low_us)) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "gpio_pulse pin cycles high low");
        return;
    }

    const uint32_t total_us = cycles * (high_us + low_us);
    if(strcmp(pin, "pc0") != 0 || cycles == 0U || cycles > TUMOSCRIPT_GPIO_MAX_CYCLES ||
       high_us < TUMOSCRIPT_GPIO_MIN_PULSE_US || low_us < TUMOSCRIPT_GPIO_MIN_PULSE_US ||
       high_us > TUMOSCRIPT_GPIO_MAX_PULSE_US || low_us > TUMOSCRIPT_GPIO_MAX_PULSE_US ||
       total_us > TUMOSCRIPT_GPIO_MAX_TOTAL_US) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "gpio_pulse out of bounds");
        return;
    }

    tumoscript_add_step(plan, TumoScriptStepGpioPulse, line_no, pin, value, cycles, high_us, low_us);
}

static void tumoscript_parse_ir_burst(TumoScriptPlan* plan, const char* value, uint32_t line_no) {
    const char* cursor = value;
    uint32_t frequency = 0;
    uint32_t duration_ms = 0;

    if(!tumoscript_parse_u32(&cursor, &frequency) ||
       !tumoscript_parse_u32(&cursor, &duration_ms)) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "ir_burst frequency duration_ms");
        return;
    }

    if(frequency < TUMOSCRIPT_IR_MIN_FREQUENCY || frequency > TUMOSCRIPT_IR_MAX_FREQUENCY ||
       duration_ms < TUMOSCRIPT_IR_MIN_DURATION_MS ||
       duration_ms > TUMOSCRIPT_IR_MAX_DURATION_MS) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "ir_burst out of bounds");
        return;
    }

    tumoscript_add_step(
        plan, TumoScriptStepIrBurst, line_no, "ir", value, frequency, duration_ms, 0);
}

static void tumoscript_parse_bridge(TumoScriptPlan* plan, const char* value, uint32_t line_no) {
    const char* cursor = value;
    char command[TUMOSCRIPT_TOKEN_SIZE];
    if(!tumoscript_parse_word(&cursor, command, sizeof(command))) {
        plan->parse_errors++;
        tumoscript_set_first_error(plan, line_no, "bridge command payload");
        return;
    }

    tumoscript_add_step(
        plan, TumoScriptStepBridge, line_no, command, tumoscript_rest(cursor), 0, 0, 0);
}

static void tumoscript_parse_line(TumoScriptPlan* plan, char* raw_line, uint32_t line_no) {
    char* line = tumoscript_trim(raw_line);
    if((line[0] == '\0') || (line[0] == '#')) return;

    const char* value = tumoscript_value_after_key(line, "name");
    if(value) {
        strlcpy(plan->name, value[0] ? value : "Unnamed Script", sizeof(plan->name));
        return;
    }

    value = tumoscript_value_after_key(line, "policy");
    if(value) {
        plan->continue_on_error =
            (strcmp(value, "continue") == 0) || (strcmp(value, "continue_on_error") == 0);
        return;
    }

    value = tumoscript_value_after_key(line, "label");
    if(value) {
        tumoscript_add_label(plan, value, line_no);
        return;
    }

    value = tumoscript_value_after_key(line, "log");
    if(value) {
        tumoscript_add_step(plan, TumoScriptStepLog, line_no, "log", value, 0, 0, 0);
        return;
    }

    value = tumoscript_value_after_key(line, "delay");
    if(value) {
        const char* cursor = value;
        uint32_t delay_ms = 0;
        if(tumoscript_parse_u32(&cursor, &delay_ms) && delay_ms <= TUMOSCRIPT_MAX_DELAY_MS) {
            tumoscript_add_step(plan, TumoScriptStepDelay, line_no, "delay", value, delay_ms, 0, 0);
        } else {
            plan->parse_errors++;
            tumoscript_set_first_error(plan, line_no, "delay out of bounds");
        }
        return;
    }

    value = tumoscript_value_after_key(line, "prompt");
    if(value) {
        tumoscript_add_step(
            plan,
            TumoScriptStepPrompt,
            line_no,
            "prompt",
            value[0] ? value : "Press OK",
            0,
            0,
            0);
        return;
    }

    value = tumoscript_value_after_key(line, "bridge");
    if(value) {
        tumoscript_parse_bridge(plan, value, line_no);
        return;
    }

    value = tumoscript_value_after_key(line, "gpio_pulse");
    if(value) {
        tumoscript_parse_gpio_pulse(plan, value, line_no);
        return;
    }

    value = tumoscript_value_after_key(line, "ir_burst");
    if(value) {
        tumoscript_parse_ir_burst(plan, value, line_no);
        return;
    }

    value = tumoscript_value_after_key(line, "branch_ok");
    if(value) {
        tumoscript_add_step(plan, TumoScriptStepBranchOk, line_no, value, value, 0, 0, 0);
        return;
    }

    value = tumoscript_value_after_key(line, "branch_error");
    if(value) {
        tumoscript_add_step(plan, TumoScriptStepBranchError, line_no, value, value, 0, 0, 0);
        return;
    }

    value = tumoscript_value_after_key(line, "goto");
    if(value) {
        tumoscript_add_step(plan, TumoScriptStepGoto, line_no, value, value, 0, 0, 0);
        return;
    }

    plan->parse_errors++;
    tumoscript_set_first_error(plan, line_no, "unsupported command");
}

static bool tumoscript_validate_plan(TumoScriptPlan* plan) {
    if(plan->step_count == 0U) {
        plan->validation_errors++;
        tumoscript_set_first_error(plan, 0, "empty script");
    }

    for(uint8_t i = 0; i < plan->step_count; i++) {
        const TumoScriptStep* step = &plan->steps[i];
        if((step->type == TumoScriptStepBranchOk) ||
           (step->type == TumoScriptStepBranchError) ||
           (step->type == TumoScriptStepGoto)) {
            if(tumoscript_find_label(plan, step->value) < 0) {
                plan->validation_errors++;
                tumoscript_set_first_error(plan, step->line_no, "missing label");
            }
        }
    }

    return (plan->parse_errors == 0U) && (plan->validation_errors == 0U);
}

static bool tumoscript_load_plan(
    TumoScriptApp* app,
    const TumoScriptFile* script,
    TumoScriptPlan* plan) {
    memset(plan, 0, sizeof(TumoScriptPlan));
    strlcpy(plan->name, script->name, sizeof(plan->name));
    strlcpy(plan->path, script->path, sizeof(plan->path));

    File* file = storage_file_alloc(app->storage);
    const bool opened = storage_file_open(file, script->path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(!opened) {
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    char buffer[64];
    char line[TUMOSCRIPT_LINE_SIZE];
    uint32_t line_no = 1;
    uint32_t total_bytes = 0;
    size_t line_len = 0;
    size_t bytes_read;
    bool line_overflow = false;
    while((bytes_read = storage_file_read(file, buffer, sizeof(buffer))) > 0U) {
        total_bytes += bytes_read;
        if(total_bytes > TUMOSCRIPT_MAX_FILE_BYTES) {
            plan->parse_errors++;
            tumoscript_set_first_error(plan, line_no, "script too large");
            break;
        }

        for(size_t i = 0; i < bytes_read; i++) {
            const char ch = buffer[i];
            if((ch == '\n') || (ch == '\r')) {
                if(line_len > 0U && !line_overflow) {
                    line[line_len] = '\0';
                    tumoscript_parse_line(plan, line, line_no);
                }
                line_len = 0;
                line_overflow = false;
                if(ch == '\n') line_no++;
            } else if(line_overflow) {
                continue;
            } else if(line_len + 1U < sizeof(line)) {
                line[line_len++] = ch;
            } else {
                plan->parse_errors++;
                tumoscript_set_first_error(plan, line_no, "line too long");
                line_overflow = true;
            }
        }
    }
    if(line_len > 0U && !line_overflow) {
        line[line_len] = '\0';
        tumoscript_parse_line(plan, line, line_no);
    }

    storage_file_close(file);
    storage_file_free(file);
    return tumoscript_validate_plan(plan);
}

static void tumoscript_write_csv_value(File* file, const char* text) {
    storage_file_write(file, "\"", 1);
    for(const char* cursor = text; *cursor; cursor++) {
        if(*cursor == '"') {
            storage_file_write(file, "\"\"", 2);
        } else {
            storage_file_write(file, cursor, 1);
        }
    }
    storage_file_write(file, "\"", 1);
}

static bool tumoscript_open_log(TumoScriptApp* app, const char* mode) {
    char timestamp[32];
    tumoscript_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        app->log_path,
        sizeof(app->log_path),
        TUMOSCRIPT_RUNS_DIR "/%s_%s.csv",
        mode,
        timestamp);

    app->log_file = storage_file_alloc(app->storage);
    const bool opened = storage_file_open(app->log_file, app->log_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(!opened) {
        storage_file_free(app->log_file);
        app->log_file = NULL;
        return false;
    }

    tumoscript_write(app->log_file, "timestamp,script,mode,step,line,command,status,detail\n");
    storage_file_sync(app->log_file);
    return true;
}

static void tumoscript_close_log(TumoScriptApp* app) {
    if(!app->log_file) return;
    storage_file_sync(app->log_file);
    storage_file_close(app->log_file);
    storage_file_free(app->log_file);
    app->log_file = NULL;
}

static void tumoscript_log_step(
    TumoScriptApp* app,
    const TumoScriptPlan* plan,
    const char* mode,
    const TumoScriptStep* step,
    uint8_t step_index,
    const char* status,
    const char* detail) {
    if(!app->log_file) return;

    char timestamp[32];
    char prefix[96];
    tumoscript_format_iso_timestamp(timestamp, sizeof(timestamp));
    snprintf(prefix, sizeof(prefix), "%s,", timestamp);
    tumoscript_write(app->log_file, prefix);
    tumoscript_write_csv_value(app->log_file, plan->name);
    tumoscript_write(app->log_file, ",");
    tumoscript_write_csv_value(app->log_file, mode);
    snprintf(
        prefix,
        sizeof(prefix),
        ",%u,%lu,",
        (unsigned)step_index,
        (unsigned long)(step ? step->line_no : 0UL));
    tumoscript_write(app->log_file, prefix);
    tumoscript_write_csv_value(app->log_file, step ? tumoscript_step_name(step->type) : "");
    tumoscript_write(app->log_file, ",");
    tumoscript_write_csv_value(app->log_file, status ? status : "");
    tumoscript_write(app->log_file, ",");
    tumoscript_write_csv_value(app->log_file, detail ? detail : "");
    tumoscript_write(app->log_file, "\n");
    storage_file_sync(app->log_file);
}

static bool tumoscript_wait_delay(TumoScriptApp* app, uint32_t delay_ms) {
    uint32_t remaining = delay_ms;
    while(remaining > 0U) {
        InputEvent input;
        const uint32_t chunk =
            remaining > TUMOSCRIPT_DELAY_CHUNK_MS ? TUMOSCRIPT_DELAY_CHUNK_MS : remaining;
        const FuriStatus status =
            furi_message_queue_get(app->queue, &input, furi_ms_to_ticks(chunk));
        if(status == FuriStatusOk) {
            if((input.type == InputTypeShort) && (input.key == InputKeyBack)) {
                app->cancel_requested = true;
                return false;
            }
        } else {
            remaining -= chunk;
        }
    }
    return true;
}

static bool tumoscript_wait_ok_or_back(TumoScriptApp* app) {
    while(true) {
        InputEvent input;
        if(furi_message_queue_get(app->queue, &input, FuriWaitForever) != FuriStatusOk) continue;
        if(input.type != InputTypeShort) continue;
        if(input.key == InputKeyBack) {
            app->cancel_requested = true;
            return false;
        }
        if(input.key == InputKeyOk) {
            return true;
        }
    }
}

static bool tumoscript_send_bridge(TumoScriptApp* app, const char* command, const char* payload) {
    const uint32_t request_id = app->request_id_next++;
    bool sent = bt_app_bridge_send_text_v2(app->bt, TUMOSCRIPT_APP_ID, command, request_id, 0, payload);
    if(!sent) {
        sent = bt_app_bridge_send_text(app->bt, TUMOSCRIPT_APP_ID, command, payload);
    }
    return sent;
}

static bool tumoscript_run_gpio_pulse(TumoScriptApp* app, const TumoScriptStep* step) {
    UNUSED(app);
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeOutputPushPull);
    for(uint32_t i = 0; i < step->a; i++) {
        furi_hal_gpio_write(&gpio_ext_pc0, true);
        furi_delay_us(step->b);
        furi_hal_gpio_write(&gpio_ext_pc0, false);
        furi_delay_us(step->c);
    }
    furi_hal_gpio_write(&gpio_ext_pc0, false);
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog);
    return true;
}

static FuriHalInfraredTxGetDataState
    tumoscript_ir_tx_callback(void* context, uint32_t* duration, bool* level) {
    TumoScriptIrTx* tx = context;
    *level = true;
    *duration = tx->remaining_us;
    tx->remaining_us = 0;
    return FuriHalInfraredTxGetDataStateLastDone;
}

static void tumoscript_ir_done_callback(void* context) {
    bool* done = context;
    *done = true;
}

static bool tumoscript_run_ir_burst(TumoScriptApp* app, const TumoScriptStep* step) {
    if(furi_hal_infrared_is_busy()) {
        return false;
    }

    volatile bool done = false;
    TumoScriptIrTx tx = {
        .remaining_us = step->b * 1000U,
    };

    const FuriHalInfraredTxPin output = furi_hal_infrared_detect_tx_output();
    furi_hal_infrared_set_tx_output(output);
    furi_hal_infrared_async_tx_set_data_isr_callback(tumoscript_ir_tx_callback, &tx);
    furi_hal_infrared_async_tx_set_signal_sent_isr_callback(
        tumoscript_ir_done_callback, (void*)&done);
    furi_hal_infrared_async_tx_start(step->a, 0.33f);

    const uint32_t start = furi_get_tick();
    const uint32_t timeout = furi_ms_to_ticks(step->b + 250U);
    while(!done && ((furi_get_tick() - start) < timeout)) {
        InputEvent input;
        const FuriStatus status =
            furi_message_queue_get(app->queue, &input, furi_ms_to_ticks(5));
        if((status == FuriStatusOk) && (input.type == InputTypeShort) &&
           (input.key == InputKeyBack)) {
            app->cancel_requested = true;
            break;
        }
    }

    furi_hal_infrared_async_tx_stop();
    furi_hal_infrared_async_tx_set_signal_sent_isr_callback(NULL, NULL);
    furi_hal_infrared_async_tx_set_data_isr_callback(NULL, NULL);
    return done && !app->cancel_requested;
}

static const char* tumoscript_step_name(TumoScriptStepType type) {
    switch(type) {
    case TumoScriptStepLabel:
        return "label";
    case TumoScriptStepLog:
        return "log";
    case TumoScriptStepDelay:
        return "delay";
    case TumoScriptStepPrompt:
        return "prompt";
    case TumoScriptStepBridge:
        return "bridge";
    case TumoScriptStepGpioPulse:
        return "gpio_pulse";
    case TumoScriptStepIrBurst:
        return "ir_burst";
    case TumoScriptStepBranchOk:
        return "branch_ok";
    case TumoScriptStepBranchError:
        return "branch_error";
    case TumoScriptStepGoto:
        return "goto";
    default:
        return "unknown";
    }
}

static void tumoscript_execute_plan(
    TumoScriptApp* app,
    const TumoScriptPlan* plan,
    TumoScriptRunMode run_mode) {
    const bool dry_run = run_mode != TumoScriptRunModeExecute;
    const char* mode_name = run_mode == TumoScriptRunModeValidate ? "validate" :
                            run_mode == TumoScriptRunModeDryRun   ? "dry_run" :
                                                                    "run";
    const bool log_opened = tumoscript_open_log(app, mode_name);
    app->cancel_requested = false;

    if(run_mode == TumoScriptRunModeValidate) {
        tumoscript_set_model(
            app,
            TumoScriptModeBrowse,
            "Valid",
            log_opened ? app->log_path : plan->name,
            0,
            plan->step_count);
        if(log_opened) {
            tumoscript_log_step(app, plan, mode_name, NULL, 0, "valid", plan->path);
        }
        tumoscript_close_log(app);
        notification_message(app->notification, &tumoscript_sequence_ok);
        return;
    }

    bool last_ok = true;
    bool stopped_on_error = false;
    uint8_t executed = 0;
    int16_t pc = 0;

    while((pc >= 0) && (pc < plan->step_count) && (executed < TUMOSCRIPT_MAX_EXECUTED_STEPS)) {
        const TumoScriptStep* step = &plan->steps[pc];
        executed++;

        char status[TUMOSCRIPT_STATUS_SIZE];
        snprintf(
            status,
            sizeof(status),
            "%s line %lu",
            tumoscript_step_name(step->type),
            (unsigned long)step->line_no);
        tumoscript_set_model(
            app, TumoScriptModeRun, status, step->value, executed, plan->step_count);

        bool ok = true;
        const char* result = "ok";
        const char* detail = step->value;
        int16_t next_pc = pc + 1;

        switch(step->type) {
        case TumoScriptStepLabel:
            break;
        case TumoScriptStepLog:
            break;
        case TumoScriptStepDelay:
            if(!dry_run) {
                ok = tumoscript_wait_delay(app, step->a);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "cancelled";
            break;
        case TumoScriptStepPrompt:
            if(!dry_run) {
                tumoscript_set_model(
                    app, TumoScriptModeRun, "prompt", step->value, executed, plan->step_count);
                ok = tumoscript_wait_ok_or_back(app);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "cancelled";
            break;
        case TumoScriptStepBridge:
            if(!dry_run) {
                ok = tumoscript_send_bridge(app, step->arg, step->value);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "send_failed";
            detail = step->arg;
            break;
        case TumoScriptStepGpioPulse:
            if(!dry_run) {
                tumoscript_set_model(
                    app,
                    TumoScriptModeRun,
                    "confirm GPIO",
                    "OK confirm / Back cancel",
                    executed,
                    plan->step_count);
                ok = tumoscript_wait_ok_or_back(app) && tumoscript_run_gpio_pulse(app, step);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "cancelled";
            break;
        case TumoScriptStepIrBurst:
            if(!dry_run) {
                tumoscript_set_model(
                    app,
                    TumoScriptModeRun,
                    "confirm IR",
                    "OK confirm / Back cancel",
                    executed,
                    plan->step_count);
                ok = tumoscript_wait_ok_or_back(app) && tumoscript_run_ir_burst(app, step);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "ir_failed";
            break;
        case TumoScriptStepBranchOk:
            if(last_ok) {
                next_pc = tumoscript_find_label(plan, step->value);
                result = "taken";
            } else {
                result = "skipped";
            }
            break;
        case TumoScriptStepBranchError:
            if(!last_ok) {
                next_pc = tumoscript_find_label(plan, step->value);
                result = "taken";
            } else {
                result = "skipped";
            }
            break;
        case TumoScriptStepGoto:
            next_pc = tumoscript_find_label(plan, step->value);
            result = "taken";
            break;
        default:
            ok = false;
            result = "unsupported";
            break;
        }

        tumoscript_log_step(app, plan, mode_name, step, executed, result, detail);
        if(app->cancel_requested) break;
        last_ok = ok;
        if(!ok && !plan->continue_on_error) {
            stopped_on_error = true;
            break;
        }
        pc = next_pc;
    }

    if(executed >= TUMOSCRIPT_MAX_EXECUTED_STEPS) {
        stopped_on_error = true;
        tumoscript_log_step(app, plan, mode_name, NULL, executed, "error", "execution limit");
    }

    tumoscript_close_log(app);
    if(app->cancel_requested) {
        tumoscript_set_model(app, TumoScriptModeBrowse, "Cancelled", plan->name, 0, 0);
        notification_message(app->notification, &tumoscript_sequence_error);
    } else if(stopped_on_error) {
        tumoscript_set_model(app, TumoScriptModeBrowse, "Stopped on error", plan->name, 0, 0);
        notification_message(app->notification, &tumoscript_sequence_error);
    } else {
        tumoscript_set_model(app, TumoScriptModeBrowse, dry_run ? "Dry-run done" : "Done", plan->name, 0, 0);
        notification_message(app->notification, &tumoscript_sequence_ok);
    }
}

static void tumoscript_run_selected(TumoScriptApp* app, TumoScriptRunMode run_mode) {
    TumoScriptFile script;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->script_count == 0U) {
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        tumoscript_set_model(app, TumoScriptModeBrowse, "No scripts", "Add .tscr files", 0, 0);
        return;
    }
    script = app->scripts[app->selected];
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    TumoScriptPlan* plan = malloc(sizeof(TumoScriptPlan));
    if(!plan) {
        tumoscript_set_model(app, TumoScriptModeBrowse, "No memory", script.name, 0, 0);
        return;
    }

    if(!tumoscript_load_plan(app, &script, plan)) {
        char detail[TUMOSCRIPT_STATUS_SIZE];
        snprintf(
            detail,
            sizeof(detail),
            "%u parse, %u validation",
            (unsigned)plan->parse_errors,
            (unsigned)plan->validation_errors);
        tumoscript_set_model(
            app,
            TumoScriptModeBrowse,
            plan->first_error[0] ? plan->first_error : "Load failed",
            detail,
            0,
            0);
        notification_message(app->notification, &tumoscript_sequence_error);
        free(plan);
        return;
    }

    tumoscript_execute_plan(app, plan, run_mode);
    free(plan);
}

static TumoScriptApp* tumoscript_alloc(void) {
    TumoScriptApp* app = malloc(sizeof(TumoScriptApp));
    furi_check(app);
    memset(app, 0, sizeof(TumoScriptApp));
    app->queue = furi_message_queue_alloc(TUMOSCRIPT_QUEUE_DEPTH, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->bt = furi_record_open(RECORD_BT);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->request_id_next = 1U;
    strlcpy(app->status, "Ready", sizeof(app->status));
    strlcpy(app->detail, "Left valid, Right dry", sizeof(app->detail));

    tumoscript_ensure_dirs(app);
    tumoscript_ensure_sample(app);
    tumoscript_scan_scripts(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tumoscript_draw_callback, app);
    view_port_input_callback_set(app->view_port, tumoscript_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void tumoscript_free(TumoScriptApp* app) {
    tumoscript_close_log(app);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tumoscript_app(void* context) {
    UNUSED(context);
    TumoScriptApp* app = tumoscript_alloc();

    bool running = true;
    while(running) {
        InputEvent input;
        if(furi_message_queue_get(app->queue, &input, FuriWaitForever) != FuriStatusOk) continue;
        if(input.type != InputTypeShort) continue;

        if(input.key == InputKeyBack) {
            running = false;
        } else if(input.key == InputKeyUp) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->script_count > 0U) {
                app->selected = (app->selected == 0U) ? (app->script_count - 1U) : (app->selected - 1U);
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            view_port_update(app->view_port);
        } else if(input.key == InputKeyDown) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->script_count > 0U) {
                app->selected = (app->selected + 1U) % app->script_count;
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            view_port_update(app->view_port);
        } else if(input.key == InputKeyLeft) {
            tumoscript_run_selected(app, TumoScriptRunModeValidate);
        } else if(input.key == InputKeyRight) {
            tumoscript_run_selected(app, TumoScriptRunModeDryRun);
        } else if(input.key == InputKeyOk) {
            tumoscript_run_selected(app, TumoScriptRunModeExecute);
        }
    }

    tumoscript_free(app);
    return 0;
}
