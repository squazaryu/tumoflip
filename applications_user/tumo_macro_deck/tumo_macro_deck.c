#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUMO_MACRO_DECK_APP_ID "tumo_macro_deck"
#define TUMO_MACRO_DECK_DATA_DIR EXT_PATH("apps_data/tumo_macro_deck")
#define TUMO_MACRO_DECK_MACROS_DIR TUMO_MACRO_DECK_DATA_DIR "/macros"
#define TUMO_MACRO_DECK_RUNS_DIR TUMO_MACRO_DECK_DATA_DIR "/runs"
#define TUMO_MACRO_DECK_SAMPLE_PATH TUMO_MACRO_DECK_MACROS_DIR "/safe_demo.tmacro"

#define TUMO_MACRO_DECK_MAX_MACROS 12U
#define TUMO_MACRO_DECK_MAX_STEPS 32U
#define TUMO_MACRO_DECK_QUEUE_DEPTH 8U
#define TUMO_MACRO_DECK_NAME_SIZE 48U
#define TUMO_MACRO_DECK_PATH_SIZE 160U
#define TUMO_MACRO_DECK_LINE_SIZE 192U
#define TUMO_MACRO_DECK_VALUE_SIZE 112U
#define TUMO_MACRO_DECK_STATUS_SIZE 64U
#define TUMO_MACRO_DECK_LOG_PATH_SIZE 160U
#define TUMO_MACRO_DECK_DELAY_CHUNK_MS 100U
#define TUMO_MACRO_DECK_MAX_DELAY_MS 600000UL

typedef enum {
    TumoMacroDeckModeBrowse,
    TumoMacroDeckModeRun,
} TumoMacroDeckMode;

typedef enum {
    TumoMacroStepLog,
    TumoMacroStepDelay,
    TumoMacroStepWaitButton,
    TumoMacroStepBleEvent,
    TumoMacroStepIr,
    TumoMacroStepGpio,
    TumoMacroStepUnsupported,
} TumoMacroStepType;

typedef struct {
    char name[TUMO_MACRO_DECK_NAME_SIZE];
    char path[TUMO_MACRO_DECK_PATH_SIZE];
} TumoMacroFile;

typedef struct {
    TumoMacroStepType type;
    uint32_t line_no;
    uint32_t delay_ms;
    char value[TUMO_MACRO_DECK_VALUE_SIZE];
} TumoMacroStep;

typedef struct {
    char name[TUMO_MACRO_DECK_NAME_SIZE];
    char path[TUMO_MACRO_DECK_PATH_SIZE];
    TumoMacroStep steps[TUMO_MACRO_DECK_MAX_STEPS];
    uint8_t step_count;
    uint8_t parse_errors;
    bool continue_on_error;
} TumoMacroPlan;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Storage* storage;
    Bt* bt;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    File* log_file;
    TumoMacroFile macros[TUMO_MACRO_DECK_MAX_MACROS];
    uint8_t macro_count;
    uint8_t selected;
    uint8_t current_step;
    uint8_t total_steps;
    uint32_t request_id_next;
    TumoMacroDeckMode mode;
    bool cancel_requested;
    char status[TUMO_MACRO_DECK_STATUS_SIZE];
    char detail[TUMO_MACRO_DECK_STATUS_SIZE];
    char log_path[TUMO_MACRO_DECK_LOG_PATH_SIZE];
} TumoMacroDeckApp;

static const char* tumo_macro_deck_step_name(TumoMacroStepType type);

static const char tumo_macro_deck_sample_macro[] =
    "# Tumo Macro Deck v1\n"
    "name Safe Demo\n"
    "policy stop\n"
    "log Macro started\n"
    "delay 250\n"
    "ble_event safe_demo_started\n"
    "wait_button Press OK\n"
    "log Macro done\n";

static bool tumo_macro_deck_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static bool tumo_macro_deck_write(File* file, const char* text) {
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size;
}

static void tumo_macro_deck_format_iso_timestamp(char* output, size_t output_size) {
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

static void tumo_macro_deck_format_filename_timestamp(char* output, size_t output_size) {
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

static void tumo_macro_deck_set_model(
    TumoMacroDeckApp* app,
    TumoMacroDeckMode mode,
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

static void tumo_macro_deck_copy_draw_text(
    char* output,
    size_t output_size,
    const char* input,
    size_t max_chars) {
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

static void tumo_macro_deck_draw_callback(Canvas* canvas, void* context) {
    TumoMacroDeckApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    char macro_name[26];
    char status[26];
    char detail[26];
    const bool has_macros = app->macro_count > 0U;
    const uint8_t selected = app->selected;
    tumo_macro_deck_copy_draw_text(
        macro_name,
        sizeof(macro_name),
        has_macros ? app->macros[selected].name : "No .tmacro files",
        24U);
    tumo_macro_deck_copy_draw_text(status, sizeof(status), app->status, 24U);
    tumo_macro_deck_copy_draw_text(detail, sizeof(detail), app->detail, 24U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Tumo Macro Deck");
    canvas_set_font(canvas, FontSecondary);

    if(app->mode == TumoMacroDeckModeBrowse) {
        char counter[28];
        snprintf(
            counter,
            sizeof(counter),
            "Macro %u/%u",
            has_macros ? (unsigned)(selected + 1U) : 0U,
            (unsigned)app->macro_count);
        canvas_draw_str(canvas, 0, 23, counter);
        canvas_draw_str(canvas, 0, 35, macro_name);
        canvas_draw_str(canvas, 0, 48, status);
        canvas_draw_str(canvas, 0, 60, detail);
        canvas_draw_str_aligned(canvas, 127, 23, AlignRight, AlignBottom, "OK Run");
    } else {
        char progress[28];
        snprintf(
            progress,
            sizeof(progress),
            "Step %u/%u",
            (unsigned)app->current_step,
            (unsigned)app->total_steps);
        canvas_draw_str(canvas, 0, 23, progress);
        canvas_draw_str(canvas, 0, 35, macro_name);
        canvas_draw_str(canvas, 0, 48, status);
        canvas_draw_str(canvas, 0, 60, detail);
        canvas_draw_str_aligned(canvas, 127, 23, AlignRight, AlignBottom, "Back Cancel");
    }

    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void tumo_macro_deck_input_callback(InputEvent* input_event, void* context) {
    TumoMacroDeckApp* app = context;
    furi_message_queue_put(app->queue, input_event, 0);
}

static bool tumo_macro_deck_name_ends_with(const char* name, const char* suffix) {
    const size_t name_len = strlen(name);
    const size_t suffix_len = strlen(suffix);
    return (name_len >= suffix_len) && (strcmp(name + name_len - suffix_len, suffix) == 0);
}

static void tumo_macro_deck_ensure_dirs(TumoMacroDeckApp* app) {
    tumo_macro_deck_mkdir(app->storage, EXT_PATH("apps_data"));
    tumo_macro_deck_mkdir(app->storage, TUMO_MACRO_DECK_DATA_DIR);
    tumo_macro_deck_mkdir(app->storage, TUMO_MACRO_DECK_MACROS_DIR);
    tumo_macro_deck_mkdir(app->storage, TUMO_MACRO_DECK_RUNS_DIR);
}

static void tumo_macro_deck_ensure_sample(TumoMacroDeckApp* app) {
    if(storage_file_exists(app->storage, TUMO_MACRO_DECK_SAMPLE_PATH)) return;

    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, TUMO_MACRO_DECK_SAMPLE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        tumo_macro_deck_write(file, tumo_macro_deck_sample_macro);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void tumo_macro_deck_scan_macros(TumoMacroDeckApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->macro_count = 0;
    app->selected = 0;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    File* directory = storage_file_alloc(app->storage);
    if(storage_dir_open(directory, TUMO_MACRO_DECK_MACROS_DIR)) {
        FileInfo file_info;
        char name[64];
        while(storage_dir_read(directory, &file_info, name, sizeof(name))) {
            if(file_info_is_dir(&file_info) || !tumo_macro_deck_name_ends_with(name, ".tmacro")) {
                continue;
            }

            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->macro_count < TUMO_MACRO_DECK_MAX_MACROS) {
                TumoMacroFile* macro = &app->macros[app->macro_count++];
                strlcpy(macro->name, name, sizeof(macro->name));
                snprintf(
                    macro->path,
                    sizeof(macro->path),
                    TUMO_MACRO_DECK_MACROS_DIR "/%s",
                    name);
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        }
    }
    storage_dir_close(directory);
    storage_file_free(directory);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    for(uint8_t i = 1; i < app->macro_count; i++) {
        TumoMacroFile current = app->macros[i];
        uint8_t j = i;
        while((j > 0U) && (strcmp(app->macros[j - 1U].name, current.name) > 0)) {
            app->macros[j] = app->macros[j - 1U];
            j--;
        }
        app->macros[j] = current;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static char* tumo_macro_deck_trim(char* text) {
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

static bool tumo_macro_deck_starts_with_key(const char* text, const char* key) {
    while(*key) {
        if(tolower((unsigned char)*text) != tolower((unsigned char)*key)) {
            return false;
        }
        text++;
        key++;
    }
    return true;
}

static const char* tumo_macro_deck_value_after_key(char* text, const char* key) {
    const size_t key_len = strlen(key);
    if(!tumo_macro_deck_starts_with_key(text, key)) return NULL;

    char separator = text[key_len];
    if(separator == '\0') return text + key_len;
    if((separator != ':') && (separator != '=') && !isspace((unsigned char)separator)) return NULL;

    char* value = text + key_len;
    if((*value == ':') || (*value == '=')) value++;
    return tumo_macro_deck_trim(value);
}

static bool tumo_macro_deck_parse_u32(const char* text, uint32_t* value) {
    uint32_t parsed = 0;
    bool has_digit = false;
    while(isspace((unsigned char)*text)) {
        text++;
    }
    while(isdigit((unsigned char)*text)) {
        has_digit = true;
        parsed = (parsed * 10U) + (uint32_t)(*text - '0');
        text++;
        if(parsed > TUMO_MACRO_DECK_MAX_DELAY_MS) {
            parsed = TUMO_MACRO_DECK_MAX_DELAY_MS;
            break;
        }
    }
    if(!has_digit) return false;
    *value = parsed;
    return true;
}

static bool tumo_macro_deck_add_step(
    TumoMacroPlan* plan,
    TumoMacroStepType type,
    uint32_t line_no,
    const char* value,
    uint32_t delay_ms) {
    if(plan->step_count >= TUMO_MACRO_DECK_MAX_STEPS) {
        plan->parse_errors++;
        return false;
    }

    TumoMacroStep* step = &plan->steps[plan->step_count++];
    step->type = type;
    step->line_no = line_no;
    step->delay_ms = delay_ms;
    strlcpy(step->value, value ? value : "", sizeof(step->value));
    return true;
}

static void tumo_macro_deck_parse_line(TumoMacroPlan* plan, char* raw_line, uint32_t line_no) {
    char* line = tumo_macro_deck_trim(raw_line);
    if((line[0] == '\0') || (line[0] == '#')) return;

    const char* value = tumo_macro_deck_value_after_key(line, "name");
    if(value) {
        strlcpy(plan->name, value[0] ? value : "Unnamed Macro", sizeof(plan->name));
        return;
    }

    value = tumo_macro_deck_value_after_key(line, "policy");
    if(value) {
        plan->continue_on_error =
            (strcmp(value, "continue") == 0) || (strcmp(value, "continue_on_error") == 0);
        return;
    }

    value = tumo_macro_deck_value_after_key(line, "log");
    if(value) {
        tumo_macro_deck_add_step(plan, TumoMacroStepLog, line_no, value, 0);
        return;
    }

    value = tumo_macro_deck_value_after_key(line, "delay");
    if(value) {
        uint32_t delay_ms = 0;
        if(tumo_macro_deck_parse_u32(value, &delay_ms)) {
            tumo_macro_deck_add_step(plan, TumoMacroStepDelay, line_no, value, delay_ms);
        } else {
            plan->parse_errors++;
        }
        return;
    }

    value = tumo_macro_deck_value_after_key(line, "wait_button");
    if(value) {
        tumo_macro_deck_add_step(
            plan, TumoMacroStepWaitButton, line_no, value[0] ? value : "Press OK", 0);
        return;
    }

    value = tumo_macro_deck_value_after_key(line, "ble_event");
    if(value) {
        tumo_macro_deck_add_step(plan, TumoMacroStepBleEvent, line_no, value, 0);
        return;
    }

    value = tumo_macro_deck_value_after_key(line, "ir");
    if(value) {
        tumo_macro_deck_add_step(plan, TumoMacroStepIr, line_no, value, 0);
        return;
    }

    value = tumo_macro_deck_value_after_key(line, "gpio");
    if(value) {
        tumo_macro_deck_add_step(plan, TumoMacroStepGpio, line_no, value, 0);
        return;
    }

    tumo_macro_deck_add_step(plan, TumoMacroStepUnsupported, line_no, line, 0);
}

static bool tumo_macro_deck_load_plan(TumoMacroDeckApp* app, const TumoMacroFile* macro, TumoMacroPlan* plan) {
    memset(plan, 0, sizeof(TumoMacroPlan));
    strlcpy(plan->name, macro->name, sizeof(plan->name));
    strlcpy(plan->path, macro->path, sizeof(plan->path));

    File* file = storage_file_alloc(app->storage);
    const bool opened = storage_file_open(file, macro->path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(!opened) {
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    char buffer[64];
    char line[TUMO_MACRO_DECK_LINE_SIZE];
    uint32_t line_no = 1;
    size_t line_len = 0;
    size_t bytes_read;
    while((bytes_read = storage_file_read(file, buffer, sizeof(buffer))) > 0U) {
        for(size_t i = 0; i < bytes_read; i++) {
            const char ch = buffer[i];
            if((ch == '\n') || (ch == '\r')) {
                if(line_len > 0U) {
                    line[line_len] = '\0';
                    tumo_macro_deck_parse_line(plan, line, line_no);
                    line_len = 0;
                }
                if(ch == '\n') line_no++;
            } else if(line_len + 1U < sizeof(line)) {
                line[line_len++] = ch;
            } else {
                plan->parse_errors++;
            }
        }
    }
    if(line_len > 0U) {
        line[line_len] = '\0';
        tumo_macro_deck_parse_line(plan, line, line_no);
    }

    storage_file_close(file);
    storage_file_free(file);
    if(plan->step_count == 0U) {
        plan->parse_errors++;
    }
    return plan->parse_errors == 0U;
}

static void tumo_macro_deck_write_csv_value(File* file, const char* text) {
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

static void tumo_macro_deck_log_step(
    TumoMacroDeckApp* app,
    const TumoMacroPlan* plan,
    const TumoMacroStep* step,
    const char* status,
    const char* detail) {
    if(!app->log_file) return;

    char timestamp[32];
    char prefix[96];
    tumo_macro_deck_format_iso_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        prefix,
        sizeof(prefix),
        "%s,",
        timestamp);
    tumo_macro_deck_write(app->log_file, prefix);
    tumo_macro_deck_write_csv_value(app->log_file, plan->name);
    snprintf(
        prefix,
        sizeof(prefix),
        ",%u,%lu,",
        (unsigned)app->current_step,
        (unsigned long)(step ? step->line_no : 0UL));
    tumo_macro_deck_write(app->log_file, prefix);
    tumo_macro_deck_write_csv_value(
        app->log_file, step ? tumo_macro_deck_step_name(step->type) : "");
    tumo_macro_deck_write(app->log_file, ",");
    tumo_macro_deck_write_csv_value(app->log_file, status ? status : "");
    tumo_macro_deck_write(app->log_file, ",");
    tumo_macro_deck_write_csv_value(app->log_file, detail ? detail : "");
    tumo_macro_deck_write(app->log_file, "\n");
    storage_file_sync(app->log_file);
}

static bool tumo_macro_deck_open_log(TumoMacroDeckApp* app) {
    char timestamp[32];
    tumo_macro_deck_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(app->log_path, sizeof(app->log_path), TUMO_MACRO_DECK_RUNS_DIR "/run_%s.csv", timestamp);

    app->log_file = storage_file_alloc(app->storage);
    const bool opened = storage_file_open(app->log_file, app->log_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(!opened) {
        storage_file_free(app->log_file);
        app->log_file = NULL;
        return false;
    }

    tumo_macro_deck_write(app->log_file, "timestamp,macro,step,line,command,status,detail\n");
    storage_file_sync(app->log_file);
    return true;
}

static void tumo_macro_deck_close_log(TumoMacroDeckApp* app) {
    if(!app->log_file) return;
    storage_file_sync(app->log_file);
    storage_file_close(app->log_file);
    storage_file_free(app->log_file);
    app->log_file = NULL;
}

static bool tumo_macro_deck_wait_delay(TumoMacroDeckApp* app, uint32_t delay_ms) {
    uint32_t remaining = delay_ms;
    while(remaining > 0U) {
        InputEvent input;
        const uint32_t chunk = remaining > TUMO_MACRO_DECK_DELAY_CHUNK_MS ?
                                   TUMO_MACRO_DECK_DELAY_CHUNK_MS :
                                   remaining;
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

static bool tumo_macro_deck_wait_ok_or_back(TumoMacroDeckApp* app) {
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

static bool tumo_macro_deck_send_ble_event(TumoMacroDeckApp* app, const char* payload) {
    const uint32_t request_id = app->request_id_next++;
    bool sent = bt_app_bridge_send_text_v2(
        app->bt, TUMO_MACRO_DECK_APP_ID, "event", request_id, 0, payload);
    if(!sent) {
        sent = bt_app_bridge_send_text(app->bt, TUMO_MACRO_DECK_APP_ID, "event", payload);
    }
    return sent;
}

static const char* tumo_macro_deck_step_name(TumoMacroStepType type) {
    switch(type) {
    case TumoMacroStepLog:
        return "log";
    case TumoMacroStepDelay:
        return "delay";
    case TumoMacroStepWaitButton:
        return "wait";
    case TumoMacroStepBleEvent:
        return "ble_event";
    case TumoMacroStepIr:
        return "ir";
    case TumoMacroStepGpio:
        return "gpio";
    case TumoMacroStepUnsupported:
    default:
        return "unsupported";
    }
}

static bool tumo_macro_deck_execute_step(
    TumoMacroDeckApp* app,
    const TumoMacroPlan* plan,
    const TumoMacroStep* step,
    uint8_t step_index) {
    char status[48];
    snprintf(status, sizeof(status), "%s line %lu", tumo_macro_deck_step_name(step->type), (unsigned long)step->line_no);
    tumo_macro_deck_set_model(
        app, TumoMacroDeckModeRun, status, step->value, step_index, plan->step_count);

    bool ok = true;
    const char* result = "ok";
    const char* detail = step->value;

    switch(step->type) {
    case TumoMacroStepLog:
        break;
    case TumoMacroStepDelay:
        ok = tumo_macro_deck_wait_delay(app, step->delay_ms);
        result = ok ? "ok" : "cancelled";
        break;
    case TumoMacroStepWaitButton:
        tumo_macro_deck_set_model(
            app, TumoMacroDeckModeRun, "wait OK", step->value, step_index, plan->step_count);
        ok = tumo_macro_deck_wait_ok_or_back(app);
        result = ok ? "ok" : "cancelled";
        break;
    case TumoMacroStepBleEvent:
        ok = tumo_macro_deck_send_ble_event(app, step->value);
        result = ok ? "ok" : "send_failed";
        break;
    case TumoMacroStepIr:
    case TumoMacroStepGpio:
        tumo_macro_deck_set_model(
            app,
            TumoMacroDeckModeRun,
            step->type == TumoMacroStepIr ? "confirm IR" : "confirm GPIO",
            "OK confirm / Back cancel",
            step_index,
            plan->step_count);
        if(!tumo_macro_deck_wait_ok_or_back(app)) {
            ok = false;
            result = "cancelled";
        } else {
            ok = false;
            result = "unsupported";
            detail = "hardware step gated";
        }
        break;
    case TumoMacroStepUnsupported:
    default:
        ok = false;
        result = "unsupported";
        break;
    }

    tumo_macro_deck_log_step(app, plan, step, result, detail);
    return ok;
}

static void tumo_macro_deck_run_selected(TumoMacroDeckApp* app) {
    TumoMacroFile macro;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->macro_count == 0U) {
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        tumo_macro_deck_set_model(
            app, TumoMacroDeckModeBrowse, "No macros", "Add .tmacro files", 0, 0);
        return;
    }
    macro = app->macros[app->selected];
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    TumoMacroPlan plan;
    if(!tumo_macro_deck_load_plan(app, &macro, &plan)) {
        char detail[48];
        snprintf(detail, sizeof(detail), "parse errors: %u", (unsigned)plan.parse_errors);
        tumo_macro_deck_set_model(app, TumoMacroDeckModeBrowse, "Load failed", detail, 0, 0);
        return;
    }

    app->cancel_requested = false;
    const bool log_opened = tumo_macro_deck_open_log(app);
    tumo_macro_deck_set_model(
        app,
        TumoMacroDeckModeRun,
        log_opened ? "Running" : "Running no log",
        plan.name,
        0,
        plan.step_count);

    bool stopped_on_error = false;
    for(uint8_t i = 0; i < plan.step_count; i++) {
        const bool ok = tumo_macro_deck_execute_step(app, &plan, &plan.steps[i], i + 1U);
        if(app->cancel_requested) {
            break;
        }
        if(!ok && !plan.continue_on_error) {
            stopped_on_error = true;
            break;
        }
    }

    tumo_macro_deck_close_log(app);
    if(app->cancel_requested) {
        tumo_macro_deck_set_model(app, TumoMacroDeckModeBrowse, "Cancelled", plan.name, 0, 0);
    } else if(stopped_on_error) {
        tumo_macro_deck_set_model(app, TumoMacroDeckModeBrowse, "Stopped on error", plan.name, 0, 0);
    } else {
        tumo_macro_deck_set_model(app, TumoMacroDeckModeBrowse, "Done", plan.name, 0, 0);
    }
}

static TumoMacroDeckApp* tumo_macro_deck_alloc(void) {
    TumoMacroDeckApp* app = malloc(sizeof(TumoMacroDeckApp));
    memset(app, 0, sizeof(TumoMacroDeckApp));
    app->queue = furi_message_queue_alloc(TUMO_MACRO_DECK_QUEUE_DEPTH, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->bt = furi_record_open(RECORD_BT);
    app->request_id_next = 1U;
    strlcpy(app->status, "Ready", sizeof(app->status));
    strlcpy(app->detail, "OK run, Up/Down select", sizeof(app->detail));

    tumo_macro_deck_ensure_dirs(app);
    tumo_macro_deck_ensure_sample(app);
    tumo_macro_deck_scan_macros(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tumo_macro_deck_draw_callback, app);
    view_port_input_callback_set(app->view_port, tumo_macro_deck_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void tumo_macro_deck_free(TumoMacroDeckApp* app) {
    tumo_macro_deck_close_log(app);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t tumo_macro_deck_app(void* context) {
    UNUSED(context);
    TumoMacroDeckApp* app = tumo_macro_deck_alloc();

    bool running = true;
    while(running) {
        InputEvent input;
        if(furi_message_queue_get(app->queue, &input, FuriWaitForever) != FuriStatusOk) continue;
        if(input.type != InputTypeShort) continue;

        if(input.key == InputKeyBack) {
            running = false;
        } else if(input.key == InputKeyUp) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->macro_count > 0U) {
                app->selected = (app->selected == 0U) ? (app->macro_count - 1U) : (app->selected - 1U);
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            view_port_update(app->view_port);
        } else if(input.key == InputKeyDown) {
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->macro_count > 0U) {
                app->selected = (app->selected + 1U) % app->macro_count;
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            view_port_update(app->view_port);
        } else if(input.key == InputKeyOk) {
            tumo_macro_deck_run_selected(app);
        } else if(input.key == InputKeyRight) {
            tumo_macro_deck_scan_macros(app);
            tumo_macro_deck_set_model(app, TumoMacroDeckModeBrowse, "Refreshed", "macro folder scanned", 0, 0);
        }
    }

    tumo_macro_deck_free(app);
    return 0;
}
