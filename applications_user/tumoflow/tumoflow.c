#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_infrared.h>
#include <furi_hal_rtc.h>
#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <ibutton/ibutton_key.h>
#include <ibutton/ibutton_protocols.h>
#include <ibutton/ibutton_worker.h>
#include <input/input.h>
#include <infrared/worker/infrared_worker.h>
#include <infrared/worker/infrared_transmit.h>
#include <lfrfid/lfrfid_worker.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <nfc/nfc.h>
#include <nfc/nfc_scanner.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/transmitter.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <toolbox/stream/stream.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUMOFLOW_APP_ID "tumoflow"
#define TUMOFLOW_DATA_DIR EXT_PATH("apps_data/tumoflow")
#define TUMOFLOW_WORKFLOWS_DIR TUMOFLOW_DATA_DIR "/workflows"
#define TUMOFLOW_RUNS_DIR TUMOFLOW_DATA_DIR "/runs"
#define TUMOFLOW_SAMPLE_PATH TUMOFLOW_WORKFLOWS_DIR "/field_demo.tflow"
#define TUMOFLOW_EDIT_TEMP_PATH TUMOFLOW_DATA_DIR "/workflow_edit.tmp"
#define TUMOFLOW_EDIT_BACKUP_PATH TUMOFLOW_DATA_DIR "/workflow_edit.bak"
#define TUMOFLOW_ONBOARDING_PATH TUMOFLOW_DATA_DIR "/onboarding-v1"
#define TUMOFLOW_LEGACY_SCRIPT_DIR EXT_PATH("apps_data/tumoscript/scripts")
#define TUMOFLOW_LEGACY_MACRO_DIR EXT_PATH("apps_data/tumo_macro_deck/macros")

#define TUMOFLOW_MAX_FILES 32U
#define TUMOFLOW_MAX_STEPS 32U
#define TUMOFLOW_MAX_LABELS 16U
#define TUMOFLOW_MAX_EXECUTED_STEPS 64U
#define TUMOFLOW_MAX_FILE_BYTES 16384U
#define TUMOFLOW_QUEUE_DEPTH 8U
#define TUMOFLOW_NAME_SIZE 48U
#define TUMOFLOW_PATH_SIZE 256U
#define TUMOFLOW_LINE_SIZE 384U
#define TUMOFLOW_VALUE_SIZE 256U
#define TUMOFLOW_TOKEN_SIZE 48U
#define TUMOFLOW_STATUS_SIZE 64U
#define TUMOFLOW_DELAY_CHUNK_MS 100U
#define TUMOFLOW_MAX_DELAY_MS 600000UL
#define TUMOFLOW_MAX_RUNTIME_MS 900000UL
#define TUMOFLOW_GPIO_MAX_CYCLES 64U
#define TUMOFLOW_GPIO_MIN_PULSE_US 50U
#define TUMOFLOW_GPIO_MAX_PULSE_US 500000U
#define TUMOFLOW_GPIO_MAX_TOTAL_US 1000000U
#define TUMOFLOW_IR_MIN_FREQUENCY 10000U
#define TUMOFLOW_IR_MAX_FREQUENCY 100000U
#define TUMOFLOW_IR_MIN_DURATION_MS 10U
#define TUMOFLOW_IR_MAX_DURATION_MS 500U
#define TUMOFLOW_SUBGHZ_TX_TIMEOUT_MS 2500U
#define TUMOFLOW_SUBGHZ_ACQUIRE_TIMEOUT_MS 500U
#define TUMOFLOW_SUBGHZ_OWNER "tumoflow"
#define TUMOFLOW_HELP_PAGE_COUNT 5U

typedef enum {
    TumoFlowModeDashboard,
    TumoFlowModeBrowse,
    TumoFlowModePreview,
    TumoFlowModeEdit,
    TumoFlowModeHelp,
    TumoFlowModeAbout,
    TumoFlowModeRun,
} TumoFlowMode;

typedef enum {
    TumoFlowRunModeValidate,
    TumoFlowRunModeDryRun,
    TumoFlowRunModeExecute,
} TumoFlowRunMode;

typedef enum {
    TumoFlowStepLabel,
    TumoFlowStepLog,
    TumoFlowStepDelay,
    TumoFlowStepPrompt,
    TumoFlowStepBridge,
    TumoFlowStepGpioPulse,
    TumoFlowStepIrBurst,
    TumoFlowStepIrFile,
    TumoFlowStepSubGhzFile,
    TumoFlowStepBranchOk,
    TumoFlowStepBranchError,
    TumoFlowStepGoto,
} TumoFlowStepType;

typedef enum {
    TumoFlowSourceNative,
    TumoFlowSourceTumoScript,
    TumoFlowSourceMacroDeck,
} TumoFlowSource;

typedef enum {
    TumoFlowTriggerManual,
    TumoFlowTriggerCountdown,
    TumoFlowTriggerIr,
    TumoFlowTriggerSubGhz,
    TumoFlowTriggerNfc,
    TumoFlowTriggerLfRfid,
    TumoFlowTriggerIButton,
} TumoFlowTriggerType;

typedef struct {
    char name[TUMOFLOW_NAME_SIZE];
    char path[TUMOFLOW_PATH_SIZE];
    TumoFlowSource source;
} TumoFlowFile;

typedef struct {
    char name[TUMOFLOW_TOKEN_SIZE];
    uint8_t step_index;
    uint32_t line_no;
} TumoFlowLabel;

typedef struct {
    TumoFlowStepType type;
    uint32_t line_no;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    char arg[TUMOFLOW_TOKEN_SIZE];
    char value[TUMOFLOW_VALUE_SIZE];
} TumoFlowStep;

typedef struct {
    char name[TUMOFLOW_NAME_SIZE];
    char path[TUMOFLOW_PATH_SIZE];
    TumoFlowSource source;
    TumoFlowTriggerType trigger;
    uint32_t trigger_delay_ms;
    char trigger_path[TUMOFLOW_PATH_SIZE];
    char trigger_signal[TUMOFLOW_TOKEN_SIZE];
    TumoFlowStep steps[TUMOFLOW_MAX_STEPS];
    TumoFlowLabel labels[TUMOFLOW_MAX_LABELS];
    uint8_t step_count;
    uint8_t label_count;
    uint8_t emission_count;
    uint8_t parse_errors;
    uint8_t validation_errors;
    bool continue_on_error;
    char first_error[TUMOFLOW_STATUS_SIZE];
} TumoFlowPlan;

typedef struct {
    uint32_t remaining_us;
} TumoFlowIrTx;

typedef struct {
    uint32_t frequency;
    FuriHalSubGhzPreset preset;
    char preset_name[48];
    char protocol[TUMOFLOW_TOKEN_SIZE];
    uint64_t key;
    uint32_t bit_count;
} TumoFlowSubGhzReference;

typedef struct {
    bool raw;
    InfraredMessage message;
    uint32_t frequency;
    float duty_cycle;
    uint32_t* timings;
    uint32_t timings_count;
} TumoFlowIrReference;

typedef struct {
    volatile bool matched;
} TumoFlowTriggerFlag;

typedef struct {
    volatile bool matched;
    InfraredMessage expected;
} TumoFlowIrTriggerContext;

typedef struct {
    volatile bool matched;
    TumoFlowSubGhzReference expected;
    FlipperFormat* format;
    SubGhzRadioPreset preset;
} TumoFlowSubGhzTriggerContext;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    Storage* storage;
    Bt* bt;
    NotificationApp* notification;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    File* log_file;
    TumoFlowFile workflows[TUMOFLOW_MAX_FILES];
    uint8_t workflow_count;
    uint8_t selected;
    uint8_t preview_page;
    uint8_t help_page;
    uint8_t current_step;
    uint8_t total_steps;
    TumoFlowTriggerType edit_trigger;
    uint32_t edit_delay_ms;
    uint32_t request_id_next;
    TumoFlowMode mode;
    bool cancel_requested;
    bool onboarding_pending;
    char status[TUMOFLOW_STATUS_SIZE];
    char detail[TUMOFLOW_STATUS_SIZE];
    char preview[3][TUMOFLOW_STATUS_SIZE];
    char log_path[TUMOFLOW_PATH_SIZE];
} TumoFlowApp;

static const NotificationSequence tumoflow_sequence_ok = {
    &message_display_backlight_on,
    &message_green_255,
    &message_delay_10,
    NULL,
};

static const NotificationSequence tumoflow_sequence_error = {
    &message_display_backlight_on,
    &message_red_255,
    &message_delay_10,
    NULL,
};

static const char tumoflow_sample_script[] =
    "# TumoFlow v1 field_demo.tflow\n"
    "name Field Demo\n"
    "trigger manual\n"
    "policy stop\n"
    "log Workflow started\n"
    "delay 250\n"
    "prompt Press OK to continue\n"
    "bridge event safe_demo_started\n"
    "label done\n"
    "log Workflow done\n";

static const char* tumoflow_step_name(TumoFlowStepType type);

static const char* tumoflow_source_name(TumoFlowSource source) {
    switch(source) {
    case TumoFlowSourceNative:
        return "FLOW";
    case TumoFlowSourceTumoScript:
        return "SCRIPT";
    case TumoFlowSourceMacroDeck:
        return "MACRO";
    default:
        return "?";
    }
}

static const char* tumoflow_trigger_name(TumoFlowTriggerType trigger) {
    switch(trigger) {
    case TumoFlowTriggerManual:
        return "Manual";
    case TumoFlowTriggerCountdown:
        return "Countdown";
    case TumoFlowTriggerIr:
        return "IR signal";
    case TumoFlowTriggerSubGhz:
        return "Sub-GHz";
    case TumoFlowTriggerNfc:
        return "NFC";
    case TumoFlowTriggerLfRfid:
        return "LF RFID";
    case TumoFlowTriggerIButton:
        return "iButton";
    default:
        return "Unknown";
    }
}

static bool tumoflow_mkdir(Storage* storage, const char* path) {
    const FS_Error error = storage_common_mkdir(storage, path);
    return (error == FSE_OK) || (error == FSE_EXIST);
}

static bool tumoflow_write(File* file, const char* text) {
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size;
}

static void tumoflow_format_iso_timestamp(char* output, size_t output_size) {
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

static void tumoflow_format_filename_timestamp(char* output, size_t output_size) {
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

static void tumoflow_set_model(
    TumoFlowApp* app,
    TumoFlowMode mode,
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
    tumoflow_copy_draw_text(char* output, size_t output_size, const char* input, size_t max_chars) {
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

static void tumoflow_draw_callback(Canvas* canvas, void* context) {
    TumoFlowApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    char workflow_name[27];
    char status[27];
    char detail[27];
    const bool has_workflows = app->workflow_count > 0U;
    const uint8_t selected = app->selected;
    tumoflow_copy_draw_text(
        workflow_name,
        sizeof(workflow_name),
        has_workflows ? app->workflows[selected].name : "No workflows",
        25U);
    tumoflow_copy_draw_text(status, sizeof(status), app->status, 25U);
    tumoflow_copy_draw_text(detail, sizeof(detail), app->detail, 25U);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "TumoFlow");
    if(has_workflows && app->mode != TumoFlowModeDashboard &&
       app->mode != TumoFlowModeHelp && app->mode != TumoFlowModeAbout) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            127,
            9,
            AlignRight,
            AlignBottom,
            tumoflow_source_name(app->workflows[selected].source));
    }
    canvas_draw_line(canvas, 0, 12, 127, 12);
    canvas_set_font(canvas, FontSecondary);

    if(app->mode == TumoFlowModeDashboard) {
        char workflow_count[32];
        snprintf(
            workflow_count,
            sizeof(workflow_count),
            "%u workflows ready",
            (unsigned)app->workflow_count);
        canvas_draw_str(canvas, 0, 24, "Field automation");
        canvas_draw_str(canvas, 0, 37, workflow_count);
        canvas_draw_str(canvas, 0, 49, "Safe, bounded, local");
        elements_button_left(canvas, "Guide");
        elements_button_center(canvas, "Open");
        elements_button_right(canvas, "About");
    } else if(app->mode == TumoFlowModeBrowse) {
        char counter[32];
        snprintf(
            counter,
            sizeof(counter),
            "%u/%u  %.20s",
            has_workflows ? (unsigned)(selected + 1U) : 0U,
            (unsigned)app->workflow_count,
            status);
        canvas_draw_str(canvas, 0, 23, workflow_name);
        canvas_draw_str(canvas, 0, 35, counter);
        canvas_draw_str(canvas, 0, 47, detail);
        elements_button_left(canvas, "Check");
        elements_button_center(canvas, "Run");
        elements_button_right(canvas, "View");
    } else if(app->mode == TumoFlowModePreview) {
        char page[32];
        snprintf(
            page,
            sizeof(page),
            "Actions %u/%u",
            (unsigned)(app->preview_page + 1U),
            (unsigned)((app->total_steps + 2U) / 3U));
        canvas_draw_str(canvas, 0, 22, page);
        canvas_draw_str(canvas, 0, 31, app->preview[0]);
        canvas_draw_str(canvas, 0, 40, app->preview[1]);
        canvas_draw_str(canvas, 0, 49, app->preview[2]);
        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Run");
        elements_button_right(canvas, "Next");
    } else if(app->mode == TumoFlowModeEdit) {
        canvas_draw_str(canvas, 0, 23, "Trigger editor");
        canvas_draw_str(canvas, 0, 36, tumoflow_trigger_name(app->edit_trigger));
        if(app->edit_trigger == TumoFlowTriggerCountdown) {
            char delay[32];
            snprintf(
                delay,
                sizeof(delay),
                "Delay %lus",
                (unsigned long)(app->edit_delay_ms / 1000U));
            canvas_draw_str(canvas, 0, 49, delay);
        } else {
            canvas_draw_str(canvas, 0, 49, "Up/Down changes type");
        }
        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Save");
        if(app->edit_trigger == TumoFlowTriggerCountdown) {
            elements_button_right(canvas, "+1s");
        }
    } else if(app->mode == TumoFlowModeHelp) {
        char page[20];
        snprintf(
            page,
            sizeof(page),
            "Guide %u/%u",
            (unsigned)(app->help_page + 1U),
            (unsigned)TUMOFLOW_HELP_PAGE_COUNT);
        canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, page);
        if(app->help_page == 0U) {
            canvas_draw_str(canvas, 0, 24, "1. Pick a workflow");
            canvas_draw_str(canvas, 0, 36, "Open, then Up/Down");
            canvas_draw_str(canvas, 0, 48, "View lists actions");
        } else if(app->help_page == 1U) {
            canvas_draw_str(canvas, 0, 24, "2. Check before run");
            canvas_draw_str(canvas, 0, 36, "Check validates file");
            canvas_draw_str(canvas, 0, 48, "Hold Right: Dry Run");
        } else if(app->help_page == 2U) {
            canvas_draw_str(canvas, 0, 24, "3. Arm outputs");
            canvas_draw_str(canvas, 0, 36, "Run shows outputs");
            canvas_draw_str(canvas, 0, 48, "OK confirms; Back stops");
        } else if(app->help_page == 3U) {
            canvas_draw_str(canvas, 0, 24, "4. Import old files");
            canvas_draw_str(canvas, 0, 36, "Select Legacy source");
            canvas_draw_str(canvas, 0, 48, "Hold OK; original stays");
        } else {
            canvas_draw_str(canvas, 0, 24, "5. Edit & recover");
            canvas_draw_str(canvas, 0, 36, "Hold Left: trigger");
            canvas_draw_str(canvas, 0, 48, "Back releases hardware");
        }
        elements_button_left(canvas, "Back");
        elements_button_right(
            canvas,
            app->help_page + 1U < TUMOFLOW_HELP_PAGE_COUNT ? "Next" : "Done");
    } else if(app->mode == TumoFlowModeAbout) {
        canvas_draw_str(canvas, 0, 24, "TumoFlow v0.2");
        canvas_draw_str(canvas, 0, 36, "Foreground workflows");
        canvas_draw_str(canvas, 0, 48, "squazaryu/tumoflip");
        elements_button_left(canvas, "Back");
    } else if(app->mode == TumoFlowModeRun) {
        char progress[30];
        snprintf(
            progress,
            sizeof(progress),
            "Step %u/%u",
            (unsigned)app->current_step,
            (unsigned)app->total_steps);
        canvas_draw_str(canvas, 0, 23, progress);
        canvas_draw_str(canvas, 0, 35, status);
        canvas_draw_str(canvas, 0, 47, detail);
        elements_button_left(canvas, "Cancel");
    }

    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void tumoflow_input_callback(InputEvent* input_event, void* context) {
    TumoFlowApp* app = context;
    furi_message_queue_put(app->queue, input_event, 0);
}

static bool tumoflow_name_ends_with(const char* name, const char* suffix) {
    const size_t name_len = strlen(name);
    const size_t suffix_len = strlen(suffix);
    return (name_len >= suffix_len) && (strcmp(name + name_len - suffix_len, suffix) == 0);
}

static void tumoflow_ensure_dirs(TumoFlowApp* app) {
    tumoflow_mkdir(app->storage, EXT_PATH("apps_data"));
    tumoflow_mkdir(app->storage, TUMOFLOW_DATA_DIR);
    tumoflow_mkdir(app->storage, TUMOFLOW_WORKFLOWS_DIR);
    tumoflow_mkdir(app->storage, TUMOFLOW_RUNS_DIR);
}

static void tumoflow_show_dashboard(TumoFlowApp* app) {
    tumoflow_set_model(app, TumoFlowModeDashboard, "Ready", "", 0U, 0U);
}

static void tumoflow_open_help(TumoFlowApp* app, uint8_t page) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->mode = TumoFlowModeHelp;
    app->help_page = page < TUMOFLOW_HELP_PAGE_COUNT ? page : 0U;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    view_port_update(app->view_port);
}

static void tumoflow_show_about(TumoFlowApp* app) {
    tumoflow_set_model(app, TumoFlowModeAbout, "", "", 0U, 0U);
}

static void tumoflow_mark_onboarding_seen(TumoFlowApp* app) {
    if(!app->onboarding_pending) return;

    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, TUMOFLOW_ONBOARDING_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        tumoflow_write(file, "TumoFlow onboarding v1\n");
        storage_file_close(file);
        app->onboarding_pending = false;
    }
    storage_file_free(file);
}

static void tumoflow_ensure_sample(TumoFlowApp* app) {
    if(storage_file_exists(app->storage, TUMOFLOW_SAMPLE_PATH)) return;

    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, TUMOFLOW_SAMPLE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        tumoflow_write(file, tumoflow_sample_script);
        storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void tumoflow_scan_dir(
    TumoFlowApp* app,
    const char* path,
    const char* suffix,
    TumoFlowSource source) {
    File* directory = storage_file_alloc(app->storage);
    if(storage_dir_open(directory, path)) {
        FileInfo file_info;
        char name[64];
        while(storage_dir_read(directory, &file_info, name, sizeof(name))) {
            if(file_info_is_dir(&file_info) || !tumoflow_name_ends_with(name, suffix)) {
                continue;
            }

            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->workflow_count < TUMOFLOW_MAX_FILES) {
                TumoFlowFile* workflow = &app->workflows[app->workflow_count++];
                strlcpy(workflow->name, name, sizeof(workflow->name));
                snprintf(workflow->path, sizeof(workflow->path), "%s/%s", path, name);
                workflow->source = source;
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);
}

static void tumoflow_scan_workflows(TumoFlowApp* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->workflow_count = 0;
    app->selected = 0;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    tumoflow_scan_dir(
        app, TUMOFLOW_WORKFLOWS_DIR, ".tflow", TumoFlowSourceNative);
    tumoflow_scan_dir(
        app, TUMOFLOW_LEGACY_SCRIPT_DIR, ".tscr", TumoFlowSourceTumoScript);
    tumoflow_scan_dir(
        app, TUMOFLOW_LEGACY_MACRO_DIR, ".tmacro", TumoFlowSourceMacroDeck);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    for(uint8_t i = 1; i < app->workflow_count; i++) {
        TumoFlowFile current = app->workflows[i];
        uint8_t j = i;
        while((j > 0U) &&
              ((app->workflows[j - 1U].source > current.source) ||
               ((app->workflows[j - 1U].source == current.source) &&
                (strcmp(app->workflows[j - 1U].name, current.name) > 0)))) {
            app->workflows[j] = app->workflows[j - 1U];
            j--;
        }
        app->workflows[j] = current;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static char* tumoflow_trim(char* text) {
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

static bool tumoflow_starts_with_key(const char* text, const char* key) {
    while(*key) {
        if(tolower((unsigned char)*text) != tolower((unsigned char)*key)) {
            return false;
        }
        text++;
        key++;
    }
    return true;
}

static const char* tumoflow_value_after_key(char* text, const char* key) {
    const size_t key_len = strlen(key);
    if(!tumoflow_starts_with_key(text, key)) return NULL;

    char separator = text[key_len];
    if(separator == '\0') return text + key_len;
    if((separator != ':') && (separator != '=') && !isspace((unsigned char)separator)) return NULL;

    char* value = text + key_len;
    if((*value == ':') || (*value == '=')) value++;
    return tumoflow_trim(value);
}

static void tumoflow_set_first_error(TumoFlowPlan* plan, uint32_t line_no, const char* error) {
    if(plan->first_error[0] == '\0') {
        snprintf(
            plan->first_error,
            sizeof(plan->first_error),
            "line %lu: %s",
            (unsigned long)line_no,
            error);
    }
}

static bool tumoflow_parse_u32(const char** cursor, uint32_t* value) {
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

static bool tumoflow_parse_word(const char** cursor, char* output, size_t output_size) {
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

static const char* tumoflow_rest(const char* cursor) {
    while(isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

static int16_t tumoflow_find_label(const TumoFlowPlan* plan, const char* name) {
    for(uint8_t i = 0; i < plan->label_count; i++) {
        if(strcmp(plan->labels[i].name, name) == 0) {
            return plan->labels[i].step_index;
        }
    }
    return -1;
}

static bool tumoflow_add_step(
    TumoFlowPlan* plan,
    TumoFlowStepType type,
    uint32_t line_no,
    const char* arg,
    const char* value,
    uint32_t a,
    uint32_t b,
    uint32_t c) {
    if(plan->step_count >= TUMOFLOW_MAX_STEPS) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "too many steps");
        return false;
    }

    TumoFlowStep* step = &plan->steps[plan->step_count++];
    memset(step, 0, sizeof(TumoFlowStep));
    step->type = type;
    step->line_no = line_no;
    step->a = a;
    step->b = b;
    step->c = c;
    strlcpy(step->arg, arg ? arg : "", sizeof(step->arg));
    strlcpy(step->value, value ? value : "", sizeof(step->value));
    if(type == TumoFlowStepGpioPulse || type == TumoFlowStepIrBurst ||
       type == TumoFlowStepIrFile || type == TumoFlowStepSubGhzFile) {
        plan->emission_count++;
    }
    return true;
}

static bool tumoflow_add_label(TumoFlowPlan* plan, const char* name, uint32_t line_no) {
    if(!name || !name[0]) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "empty label");
        return false;
    }
    if(tumoflow_find_label(plan, name) >= 0) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "duplicate label");
        return false;
    }
    if(plan->label_count >= TUMOFLOW_MAX_LABELS) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "too many labels");
        return false;
    }

    TumoFlowLabel* label = &plan->labels[plan->label_count++];
    strlcpy(label->name, name, sizeof(label->name));
    label->step_index = plan->step_count;
    label->line_no = line_no;
    return tumoflow_add_step(plan, TumoFlowStepLabel, line_no, name, name, 0, 0, 0);
}

static void tumoflow_parse_gpio_pulse(TumoFlowPlan* plan, const char* value, uint32_t line_no) {
    const char* cursor = value;
    char pin[TUMOFLOW_TOKEN_SIZE];
    uint32_t cycles = 0;
    uint32_t high_us = 0;
    uint32_t low_us = 0;

    if(!tumoflow_parse_word(&cursor, pin, sizeof(pin)) ||
       !tumoflow_parse_u32(&cursor, &cycles) ||
       !tumoflow_parse_u32(&cursor, &high_us) ||
       !tumoflow_parse_u32(&cursor, &low_us)) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "gpio_pulse pin cycles high low");
        return;
    }

    const uint32_t total_us = cycles * (high_us + low_us);
    if(strcmp(pin, "pc0") != 0 || cycles == 0U || cycles > TUMOFLOW_GPIO_MAX_CYCLES ||
       high_us < TUMOFLOW_GPIO_MIN_PULSE_US || low_us < TUMOFLOW_GPIO_MIN_PULSE_US ||
       high_us > TUMOFLOW_GPIO_MAX_PULSE_US || low_us > TUMOFLOW_GPIO_MAX_PULSE_US ||
       total_us > TUMOFLOW_GPIO_MAX_TOTAL_US) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "gpio_pulse out of bounds");
        return;
    }

    tumoflow_add_step(plan, TumoFlowStepGpioPulse, line_no, pin, value, cycles, high_us, low_us);
}

static void tumoflow_parse_ir_burst(TumoFlowPlan* plan, const char* value, uint32_t line_no) {
    const char* cursor = value;
    uint32_t frequency = 0;
    uint32_t duration_ms = 0;

    if(!tumoflow_parse_u32(&cursor, &frequency) ||
       !tumoflow_parse_u32(&cursor, &duration_ms)) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "ir_burst frequency duration_ms");
        return;
    }

    if(frequency < TUMOFLOW_IR_MIN_FREQUENCY || frequency > TUMOFLOW_IR_MAX_FREQUENCY ||
       duration_ms < TUMOFLOW_IR_MIN_DURATION_MS ||
       duration_ms > TUMOFLOW_IR_MAX_DURATION_MS) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "ir_burst out of bounds");
        return;
    }

    tumoflow_add_step(
        plan, TumoFlowStepIrBurst, line_no, "ir", value, frequency, duration_ms, 0);
}

static void tumoflow_parse_bridge(TumoFlowPlan* plan, const char* value, uint32_t line_no) {
    const char* cursor = value;
    char command[TUMOFLOW_TOKEN_SIZE];
    if(!tumoflow_parse_word(&cursor, command, sizeof(command))) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "bridge command payload");
        return;
    }

    tumoflow_add_step(
        plan, TumoFlowStepBridge, line_no, command, tumoflow_rest(cursor), 0, 0, 0);
}

static void tumoflow_parse_ir_reference(
    TumoFlowPlan* plan,
    TumoFlowStepType type,
    const char* value,
    uint32_t line_no) {
    char reference[TUMOFLOW_VALUE_SIZE];
    char signal[TUMOFLOW_TOKEN_SIZE] = {0};
    strlcpy(reference, value, sizeof(reference));
    char* separator = strrchr(reference, '|');
    if(separator != NULL) {
        *separator = '\0';
        strlcpy(signal, tumoflow_trim(separator + 1U), sizeof(signal));
    }
    char* path = tumoflow_trim(reference);
    if(path[0] == '\0' || !tumoflow_name_ends_with(path, ".ir")) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "IR reference must be .ir path");
        return;
    }
    if(type == TumoFlowStepIrFile) {
        tumoflow_add_step(plan, type, line_no, signal, path, 0, 0, 0);
    } else {
        plan->trigger = TumoFlowTriggerIr;
        strlcpy(plan->trigger_path, path, sizeof(plan->trigger_path));
        strlcpy(plan->trigger_signal, signal, sizeof(plan->trigger_signal));
    }
}

static void tumoflow_parse_trigger(TumoFlowPlan* plan, const char* value, uint32_t line_no) {
    const char* cursor = value;
    char type[TUMOFLOW_TOKEN_SIZE];
    if(!tumoflow_parse_word(&cursor, type, sizeof(type))) {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "missing trigger type");
        return;
    }

    const char* argument = tumoflow_rest(cursor);
    if(strcmp(type, "manual") == 0) {
        plan->trigger = TumoFlowTriggerManual;
    } else if(strcmp(type, "countdown") == 0) {
        uint32_t delay_ms = 0;
        if(!tumoflow_parse_u32(&argument, &delay_ms) || delay_ms > TUMOFLOW_MAX_DELAY_MS) {
            plan->parse_errors++;
            tumoflow_set_first_error(plan, line_no, "countdown out of bounds");
            return;
        }
        plan->trigger = TumoFlowTriggerCountdown;
        plan->trigger_delay_ms = delay_ms;
    } else if(strcmp(type, "ir") == 0) {
        tumoflow_parse_ir_reference(plan, TumoFlowStepLabel, argument, line_no);
    } else if(strcmp(type, "subghz") == 0) {
        if(argument[0] == '\0' || !tumoflow_name_ends_with(argument, ".sub")) {
            plan->parse_errors++;
            tumoflow_set_first_error(plan, line_no, "Sub-GHz trigger must be .sub path");
            return;
        }
        plan->trigger = TumoFlowTriggerSubGhz;
        strlcpy(plan->trigger_path, argument, sizeof(plan->trigger_path));
    } else if(strcmp(type, "nfc") == 0) {
        plan->trigger = TumoFlowTriggerNfc;
    } else if(strcmp(type, "lf_rfid") == 0) {
        plan->trigger = TumoFlowTriggerLfRfid;
    } else if(strcmp(type, "ibutton") == 0) {
        plan->trigger = TumoFlowTriggerIButton;
    } else {
        plan->parse_errors++;
        tumoflow_set_first_error(plan, line_no, "unsupported trigger");
    }
}

static void tumoflow_parse_line(TumoFlowPlan* plan, char* raw_line, uint32_t line_no) {
    char* line = tumoflow_trim(raw_line);
    if((line[0] == '\0') || (line[0] == '#')) return;

    const char* value = tumoflow_value_after_key(line, "name");
    if(value) {
        strlcpy(plan->name, value[0] ? value : "Unnamed Workflow", sizeof(plan->name));
        return;
    }

    value = tumoflow_value_after_key(line, "trigger");
    if(value) {
        tumoflow_parse_trigger(plan, value, line_no);
        return;
    }

    value = tumoflow_value_after_key(line, "policy");
    if(value) {
        plan->continue_on_error =
            (strcmp(value, "continue") == 0) || (strcmp(value, "continue_on_error") == 0);
        return;
    }

    value = tumoflow_value_after_key(line, "label");
    if(value) {
        tumoflow_add_label(plan, value, line_no);
        return;
    }

    value = tumoflow_value_after_key(line, "log");
    if(value) {
        tumoflow_add_step(plan, TumoFlowStepLog, line_no, "log", value, 0, 0, 0);
        return;
    }

    value = tumoflow_value_after_key(line, "delay");
    if(value) {
        const char* cursor = value;
        uint32_t delay_ms = 0;
        if(tumoflow_parse_u32(&cursor, &delay_ms) && delay_ms <= TUMOFLOW_MAX_DELAY_MS) {
            tumoflow_add_step(plan, TumoFlowStepDelay, line_no, "delay", value, delay_ms, 0, 0);
        } else {
            plan->parse_errors++;
            tumoflow_set_first_error(plan, line_no, "delay out of bounds");
        }
        return;
    }

    value = tumoflow_value_after_key(line, "prompt");
    if(!value) value = tumoflow_value_after_key(line, "wait_button");
    if(value) {
        tumoflow_add_step(
            plan,
            TumoFlowStepPrompt,
            line_no,
            "prompt",
            value[0] ? value : "Press OK",
            0,
            0,
            0);
        return;
    }

    value = tumoflow_value_after_key(line, "bridge");
    if(value) {
        tumoflow_parse_bridge(plan, value, line_no);
        return;
    }

    value = tumoflow_value_after_key(line, "ble_event");
    if(value) {
        tumoflow_add_step(plan, TumoFlowStepBridge, line_no, "event", value, 0, 0, 0);
        return;
    }

    value = tumoflow_value_after_key(line, "gpio_pulse");
    if(value) {
        tumoflow_parse_gpio_pulse(plan, value, line_no);
        return;
    }

    value = tumoflow_value_after_key(line, "ir_burst");
    if(value) {
        tumoflow_parse_ir_burst(plan, value, line_no);
        return;
    }

    value = tumoflow_value_after_key(line, "ir_file");
    if(value) {
        tumoflow_parse_ir_reference(plan, TumoFlowStepIrFile, value, line_no);
        return;
    }

    value = tumoflow_value_after_key(line, "subghz_file");
    if(value) {
        if(value[0] == '\0' || !tumoflow_name_ends_with(value, ".sub")) {
            plan->parse_errors++;
            tumoflow_set_first_error(plan, line_no, "subghz_file must be .sub path");
        } else {
            tumoflow_add_step(
                plan, TumoFlowStepSubGhzFile, line_no, "subghz", value, 0, 0, 0);
        }
        return;
    }

    value = tumoflow_value_after_key(line, "branch_ok");
    if(value) {
        tumoflow_add_step(plan, TumoFlowStepBranchOk, line_no, value, value, 0, 0, 0);
        return;
    }

    value = tumoflow_value_after_key(line, "branch_error");
    if(value) {
        tumoflow_add_step(plan, TumoFlowStepBranchError, line_no, value, value, 0, 0, 0);
        return;
    }

    value = tumoflow_value_after_key(line, "goto");
    if(value) {
        tumoflow_add_step(plan, TumoFlowStepGoto, line_no, value, value, 0, 0, 0);
        return;
    }

    plan->parse_errors++;
    tumoflow_set_first_error(plan, line_no, "unsupported command");
}

static bool tumoflow_subghz_preset_from_name(
    const char* name,
    FuriHalSubGhzPreset* preset) {
    if(strcmp(name, "FuriHalSubGhzPresetOok270Async") == 0) {
        *preset = FuriHalSubGhzPresetOok270Async;
    } else if(strcmp(name, "FuriHalSubGhzPresetOok650Async") == 0) {
        *preset = FuriHalSubGhzPresetOok650Async;
    } else if(strcmp(name, "FuriHalSubGhzPreset2FSKDev238Async") == 0) {
        *preset = FuriHalSubGhzPreset2FSKDev238Async;
    } else if(strcmp(name, "FuriHalSubGhzPreset2FSKDev12KAsync") == 0) {
        *preset = FuriHalSubGhzPreset2FSKDev12KAsync;
    } else if(strcmp(name, "FuriHalSubGhzPreset2FSKDev476Async") == 0) {
        *preset = FuriHalSubGhzPreset2FSKDev476Async;
    } else if(strcmp(name, "FuriHalSubGhzPresetMSK99_97KbAsync") == 0) {
        *preset = FuriHalSubGhzPresetMSK99_97KbAsync;
    } else if(strcmp(name, "FuriHalSubGhzPresetGFSK9_99KbAsync") == 0) {
        *preset = FuriHalSubGhzPresetGFSK9_99KbAsync;
    } else {
        return false;
    }
    return true;
}

static uint64_t tumoflow_key_from_bytes(const uint8_t bytes[8]) {
    uint64_t key = 0U;
    for(size_t i = 0; i < 8U; i++) {
        key = (key << 8U) | bytes[i];
    }
    return key;
}

static bool tumoflow_load_subghz_reference(
    Storage* storage,
    const char* path,
    bool for_transmit,
    TumoFlowSubGhzReference* reference,
    char* error,
    size_t error_size) {
    memset(reference, 0, sizeof(*reference));
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* preset = furi_string_alloc();
    FuriString* protocol_name = furi_string_alloc();
    uint32_t version = 0U;
    uint8_t key_bytes[8] = {0};
    bool success = false;

    do {
        if(!flipper_format_file_open_existing(format, path)) {
            strlcpy(error, "Sub-GHz file missing", error_size);
            break;
        }
        if(!flipper_format_read_header(format, header, &version) ||
           furi_string_cmp_str(header, SUBGHZ_KEY_FILE_TYPE) != 0 ||
           version != SUBGHZ_KEY_FILE_VERSION) {
            strlcpy(error, "Static .sub required", error_size);
            break;
        }
        if(!flipper_format_read_uint32(format, "Frequency", &reference->frequency, 1U) ||
           !flipper_format_read_string(format, "Preset", preset) ||
           !flipper_format_read_string(format, "Protocol", protocol_name) ||
           !flipper_format_read_uint32(format, "Bit", &reference->bit_count, 1U) ||
           !flipper_format_read_hex(format, "Key", key_bytes, sizeof(key_bytes))) {
            strlcpy(error, "Incomplete .sub file", error_size);
            break;
        }
        if(!tumoflow_subghz_preset_from_name(
               furi_string_get_cstr(preset), &reference->preset)) {
            strlcpy(error, "Custom preset unsupported", error_size);
            break;
        }
        strlcpy(
            reference->preset_name,
            furi_string_get_cstr(preset),
            sizeof(reference->preset_name));
        const SubGhzProtocol* protocol = subghz_protocol_registry_get_by_name(
            &subghz_protocol_registry, furi_string_get_cstr(protocol_name));
        if(protocol == NULL || protocol->type != SubGhzProtocolTypeStatic ||
           (for_transmit ?
                ((protocol->flag & SubGhzProtocolFlag_Send) == 0U ||
                 protocol->encoder == NULL) :
                (protocol->decoder == NULL))) {
            strlcpy(error, "Approved static protocol required", error_size);
            break;
        }
        if(reference->bit_count == 0U || reference->bit_count > 64U) {
            strlcpy(error, "Invalid Sub-GHz bit count", error_size);
            break;
        }
        strlcpy(
            reference->protocol,
            furi_string_get_cstr(protocol_name),
            sizeof(reference->protocol));
        reference->key = tumoflow_key_from_bytes(key_bytes);
        success = true;
    } while(false);

    furi_string_free(protocol_name);
    furi_string_free(preset);
    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

static void tumoflow_ir_reference_clear(TumoFlowIrReference* reference) {
    free(reference->timings);
    memset(reference, 0, sizeof(*reference));
}

static bool tumoflow_ir_message_valid(const InfraredMessage* message) {
    if(!infrared_is_protocol_valid(message->protocol)) return false;
    const uint32_t address_bits = infrared_get_protocol_address_length(message->protocol);
    const uint32_t command_bits = infrared_get_protocol_command_length(message->protocol);
    const uint32_t address_mask =
        address_bits >= 32U ? UINT32_MAX : ((1UL << address_bits) - 1UL);
    const uint32_t command_mask =
        command_bits >= 32U ? UINT32_MAX : ((1UL << command_bits) - 1UL);
    return message->address == (message->address & address_mask) &&
           message->command == (message->command & command_mask);
}

static bool tumoflow_load_ir_reference(
    Storage* storage,
    const char* path,
    const char* signal_name,
    bool decoded_only,
    TumoFlowIrReference* reference,
    char* error,
    size_t error_size) {
    memset(reference, 0, sizeof(*reference));
    FlipperFormat* format = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    FuriString* type = furi_string_alloc();
    FuriString* protocol = furi_string_alloc();
    uint32_t version = 0U;
    bool success = false;
    do {
        if(!flipper_format_file_open_existing(format, path)) {
            strlcpy(error, "IR file missing", error_size);
            break;
        }
        if(!flipper_format_read_header(format, header, &version) ||
           furi_string_cmp_str(header, "IR signals file") != 0 || version != 1U) {
            strlcpy(error, "Invalid IR file", error_size);
            break;
        }

        bool found = false;
        while(flipper_format_read_string(format, "name", name)) {
            if(signal_name[0] == '\0' || furi_string_cmp_str(name, signal_name) == 0) {
                found = true;
                break;
            }
        }
        if(!found || !flipper_format_read_string(format, "type", type)) {
            strlcpy(error, "IR signal not found", error_size);
            break;
        }

        if(furi_string_cmp_str(type, "parsed") == 0) {
            reference->raw = false;
            if(!flipper_format_read_string(format, "protocol", protocol) ||
               !flipper_format_read_hex(
                   format, "address", (uint8_t*)&reference->message.address, 4U) ||
               !flipper_format_read_hex(
                   format, "command", (uint8_t*)&reference->message.command, 4U)) {
                strlcpy(error, "Incomplete decoded IR signal", error_size);
                break;
            }
            reference->message.protocol =
                infrared_get_protocol_by_name(furi_string_get_cstr(protocol));
            reference->message.repeat = false;
            if(!tumoflow_ir_message_valid(&reference->message)) {
                strlcpy(error, "Invalid decoded IR signal", error_size);
                break;
            }
        } else if(furi_string_cmp_str(type, "raw") == 0) {
            reference->raw = true;
            if(decoded_only) {
                strlcpy(error, "Decoded IR trigger required", error_size);
                break;
            }
            if(!flipper_format_read_uint32(
                   format, "frequency", &reference->frequency, 1U) ||
               !flipper_format_read_float(
                   format, "duty_cycle", &reference->duty_cycle, 1U) ||
               !flipper_format_get_value_count(
                   format, "data", &reference->timings_count) ||
               reference->timings_count == 0U ||
               reference->timings_count > MAX_TIMINGS_AMOUNT) {
                strlcpy(error, "Incomplete raw IR signal", error_size);
                break;
            }
            reference->timings =
                malloc(reference->timings_count * sizeof(reference->timings[0]));
            if(reference->timings == NULL ||
               !flipper_format_read_uint32(
                   format,
                   "data",
                   reference->timings,
                   reference->timings_count)) {
                strlcpy(error, "Cannot load raw IR data", error_size);
                break;
            }
        } else {
            strlcpy(error, "Unknown IR signal type", error_size);
            break;
        }

        if(decoded_only && reference->raw) {
            strlcpy(error, "Decoded IR trigger required", error_size);
            break;
        }
        if(reference->raw) {
            uint64_t total_us = 0U;
            for(size_t i = 0; i < reference->timings_count; i++) {
                total_us += reference->timings[i];
            }
            if(reference->frequency < TUMOFLOW_IR_MIN_FREQUENCY ||
               reference->frequency > TUMOFLOW_IR_MAX_FREQUENCY ||
               reference->duty_cycle <= 0.0f || reference->duty_cycle > 1.0f ||
               total_us > (uint64_t)TUMOFLOW_IR_MAX_DURATION_MS * 1000U) {
                strlcpy(error, "IR file exceeds safety limit", error_size);
                break;
            }
        }
        success = true;
    } while(false);
    if(!success) tumoflow_ir_reference_clear(reference);
    furi_string_free(protocol);
    furi_string_free(type);
    furi_string_free(name);
    furi_string_free(header);
    flipper_format_free(format);
    return success;
}

static bool tumoflow_validate_plan(TumoFlowApp* app, TumoFlowPlan* plan) {
    if(plan->step_count == 0U) {
        plan->validation_errors++;
        tumoflow_set_first_error(plan, 0, "empty workflow");
    }

    uint32_t bounded_runtime_ms = plan->trigger_delay_ms;
    for(uint8_t i = 0; i < plan->step_count; i++) {
        const TumoFlowStep* step = &plan->steps[i];
        if(step->type == TumoFlowStepDelay) {
            bounded_runtime_ms += step->a;
        }
        if((step->type == TumoFlowStepBranchOk) ||
           (step->type == TumoFlowStepBranchError) ||
           (step->type == TumoFlowStepGoto)) {
            if(tumoflow_find_label(plan, step->value) < 0) {
                plan->validation_errors++;
                tumoflow_set_first_error(plan, step->line_no, "missing label");
            }
        }

        char reference_error[TUMOFLOW_STATUS_SIZE] = {0};
        if(step->type == TumoFlowStepIrFile) {
            TumoFlowIrReference reference;
            if(!tumoflow_load_ir_reference(
                   app->storage,
                   step->value,
                   step->arg,
                   false,
                   &reference,
                   reference_error,
                   sizeof(reference_error))) {
                plan->validation_errors++;
                tumoflow_set_first_error(plan, step->line_no, reference_error);
            }
            tumoflow_ir_reference_clear(&reference);
        } else if(step->type == TumoFlowStepSubGhzFile) {
            TumoFlowSubGhzReference reference;
            if(!tumoflow_load_subghz_reference(
                   app->storage,
                   step->value,
                   true,
                   &reference,
                   reference_error,
                   sizeof(reference_error))) {
                plan->validation_errors++;
                tumoflow_set_first_error(plan, step->line_no, reference_error);
            }
        }
    }

    if(plan->trigger == TumoFlowTriggerIr) {
        char reference_error[TUMOFLOW_STATUS_SIZE] = {0};
        TumoFlowIrReference reference;
        if(!tumoflow_load_ir_reference(
               app->storage,
               plan->trigger_path,
               plan->trigger_signal,
               true,
               &reference,
               reference_error,
               sizeof(reference_error))) {
            plan->validation_errors++;
            tumoflow_set_first_error(plan, 0U, reference_error);
        }
        tumoflow_ir_reference_clear(&reference);
    } else if(plan->trigger == TumoFlowTriggerSubGhz) {
        char reference_error[TUMOFLOW_STATUS_SIZE] = {0};
        TumoFlowSubGhzReference reference;
        if(!tumoflow_load_subghz_reference(
               app->storage,
               plan->trigger_path,
               false,
               &reference,
               reference_error,
               sizeof(reference_error))) {
            plan->validation_errors++;
            tumoflow_set_first_error(plan, 0U, reference_error);
        }
    }

    if(plan->emission_count > 8U) {
        plan->validation_errors++;
        tumoflow_set_first_error(plan, 0U, "too many emission actions");
    }
    if(bounded_runtime_ms > TUMOFLOW_MAX_RUNTIME_MS) {
        plan->validation_errors++;
        tumoflow_set_first_error(plan, 0U, "runtime limit exceeded");
    }

    return (plan->parse_errors == 0U) && (plan->validation_errors == 0U);
}

static bool tumoflow_load_plan(
    TumoFlowApp* app,
    const TumoFlowFile* workflow,
    TumoFlowPlan* plan) {
    memset(plan, 0, sizeof(TumoFlowPlan));
    strlcpy(plan->name, workflow->name, sizeof(plan->name));
    strlcpy(plan->path, workflow->path, sizeof(plan->path));
    plan->source = workflow->source;
    plan->trigger = TumoFlowTriggerManual;

    File* file = storage_file_alloc(app->storage);
    const bool opened =
        storage_file_open(file, workflow->path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(!opened) {
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }

    char buffer[64];
    char line[TUMOFLOW_LINE_SIZE];
    uint32_t line_no = 1;
    uint32_t total_bytes = 0;
    size_t line_len = 0;
    size_t bytes_read;
    bool line_overflow = false;
    while((bytes_read = storage_file_read(file, buffer, sizeof(buffer))) > 0U) {
        total_bytes += bytes_read;
        if(total_bytes > TUMOFLOW_MAX_FILE_BYTES) {
            plan->parse_errors++;
            tumoflow_set_first_error(plan, line_no, "script too large");
            break;
        }

        for(size_t i = 0; i < bytes_read; i++) {
            const char ch = buffer[i];
            if((ch == '\n') || (ch == '\r')) {
                if(line_len > 0U && !line_overflow) {
                    line[line_len] = '\0';
                    tumoflow_parse_line(plan, line, line_no);
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
                tumoflow_set_first_error(plan, line_no, "line too long");
                line_overflow = true;
            }
        }
    }
    if(line_len > 0U && !line_overflow) {
        line[line_len] = '\0';
        tumoflow_parse_line(plan, line, line_no);
    }

    storage_file_close(file);
    storage_file_free(file);
    return tumoflow_validate_plan(app, plan);
}

static void tumoflow_write_csv_value(File* file, const char* text) {
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

static bool tumoflow_open_log(TumoFlowApp* app, const char* mode) {
    char timestamp[32];
    tumoflow_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        app->log_path,
        sizeof(app->log_path),
        TUMOFLOW_RUNS_DIR "/%s_%s.csv",
        mode,
        timestamp);

    app->log_file = storage_file_alloc(app->storage);
    const bool opened = storage_file_open(app->log_file, app->log_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(!opened) {
        storage_file_free(app->log_file);
        app->log_file = NULL;
        return false;
    }

    tumoflow_write(app->log_file, "timestamp,script,mode,step,line,command,status,detail\n");
    storage_file_sync(app->log_file);
    return true;
}

static void tumoflow_close_log(TumoFlowApp* app) {
    if(!app->log_file) return;
    storage_file_sync(app->log_file);
    storage_file_close(app->log_file);
    storage_file_free(app->log_file);
    app->log_file = NULL;
}

static void tumoflow_log_step(
    TumoFlowApp* app,
    const TumoFlowPlan* plan,
    const char* mode,
    const TumoFlowStep* step,
    uint8_t step_index,
    const char* status,
    const char* detail) {
    if(!app->log_file) return;

    char timestamp[32];
    char prefix[96];
    tumoflow_format_iso_timestamp(timestamp, sizeof(timestamp));
    snprintf(prefix, sizeof(prefix), "%s,", timestamp);
    tumoflow_write(app->log_file, prefix);
    tumoflow_write_csv_value(app->log_file, plan->name);
    tumoflow_write(app->log_file, ",");
    tumoflow_write_csv_value(app->log_file, mode);
    snprintf(
        prefix,
        sizeof(prefix),
        ",%u,%lu,",
        (unsigned)step_index,
        (unsigned long)(step ? step->line_no : 0UL));
    tumoflow_write(app->log_file, prefix);
    tumoflow_write_csv_value(app->log_file, step ? tumoflow_step_name(step->type) : "");
    tumoflow_write(app->log_file, ",");
    tumoflow_write_csv_value(app->log_file, status ? status : "");
    tumoflow_write(app->log_file, ",");
    tumoflow_write_csv_value(app->log_file, detail ? detail : "");
    tumoflow_write(app->log_file, "\n");
    storage_file_sync(app->log_file);
}

static bool tumoflow_wait_delay(TumoFlowApp* app, uint32_t delay_ms) {
    uint32_t remaining = delay_ms;
    while(remaining > 0U) {
        InputEvent input;
        const uint32_t chunk =
            remaining > TUMOFLOW_DELAY_CHUNK_MS ? TUMOFLOW_DELAY_CHUNK_MS : remaining;
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

static bool tumoflow_wait_ok_or_back(TumoFlowApp* app) {
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

static bool tumoflow_wait_trigger_flag(
    TumoFlowApp* app,
    volatile bool* matched,
    uint32_t timeout_ms) {
    const uint32_t started = furi_get_tick();
    while(!*matched) {
        InputEvent input;
        if(furi_message_queue_get(
               app->queue, &input, furi_ms_to_ticks(50U)) == FuriStatusOk &&
           input.type == InputTypeShort && input.key == InputKeyBack) {
            app->cancel_requested = true;
            return false;
        }
        if((furi_get_tick() - started) >= furi_ms_to_ticks(timeout_ms)) return false;
    }
    return true;
}

static void tumoflow_nfc_trigger_callback(NfcScannerEvent event, void* context) {
    TumoFlowTriggerFlag* flag = context;
    if(event.type == NfcScannerEventTypeDetected) flag->matched = true;
}

static void tumoflow_lfrfid_trigger_callback(
    LFRFIDWorkerReadResult result,
    ProtocolId protocol,
    void* context) {
    UNUSED(protocol);
    TumoFlowTriggerFlag* flag = context;
    if(result == LFRFIDWorkerReadDone) flag->matched = true;
}

static void tumoflow_ibutton_trigger_callback(void* context) {
    TumoFlowTriggerFlag* flag = context;
    flag->matched = true;
}

static void tumoflow_ir_trigger_callback(
    void* context,
    InfraredWorkerSignal* received_signal) {
    TumoFlowIrTriggerContext* trigger = context;
    if(!infrared_worker_signal_is_decoded(received_signal)) return;
    const InfraredMessage* message = infrared_worker_get_decoded_signal(received_signal);
    if(message->protocol == trigger->expected.protocol &&
       message->address == trigger->expected.address &&
       message->command == trigger->expected.command) {
        trigger->matched = true;
    }
}

static void tumoflow_subghz_trigger_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    UNUSED(receiver);
    TumoFlowSubGhzTriggerContext* trigger = context;
    if(trigger->matched ||
       strcmp(decoder_base->protocol->name, trigger->expected.protocol) != 0) {
        return;
    }

    Stream* stream = flipper_format_get_raw_stream(trigger->format);
    stream_clean(stream);
    if(subghz_protocol_decoder_base_serialize(
           decoder_base, trigger->format, &trigger->preset) !=
           SubGhzProtocolStatusOk ||
       !flipper_format_rewind(trigger->format)) {
        return;
    }

    uint32_t bit_count = 0U;
    uint8_t key_bytes[8] = {0};
    if(flipper_format_read_uint32(trigger->format, "Bit", &bit_count, 1U) &&
       flipper_format_read_hex(trigger->format, "Key", key_bytes, sizeof(key_bytes)) &&
       bit_count == trigger->expected.bit_count &&
       tumoflow_key_from_bytes(key_bytes) == trigger->expected.key) {
        trigger->matched = true;
    }
}

static bool tumoflow_wait_nfc_trigger(TumoFlowApp* app) {
    TumoFlowTriggerFlag flag = {0};
    Nfc* nfc = nfc_alloc();
    NfcScanner* scanner = nfc_scanner_alloc(nfc);
    nfc_scanner_start(scanner, tumoflow_nfc_trigger_callback, &flag);
    const bool matched =
        tumoflow_wait_trigger_flag(app, &flag.matched, TUMOFLOW_MAX_RUNTIME_MS);
    nfc_scanner_stop(scanner);
    nfc_scanner_free(scanner);
    nfc_free(nfc);
    return matched;
}

static bool tumoflow_wait_lfrfid_trigger(TumoFlowApp* app) {
    TumoFlowTriggerFlag flag = {0};
    ProtocolDict* dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    LFRFIDWorker* worker = lfrfid_worker_alloc(dict);
    lfrfid_worker_start_thread(worker);
    lfrfid_worker_read_start(
        worker, LFRFIDWorkerReadTypeAuto, tumoflow_lfrfid_trigger_callback, &flag);
    const bool matched =
        tumoflow_wait_trigger_flag(app, &flag.matched, TUMOFLOW_MAX_RUNTIME_MS);
    lfrfid_worker_stop(worker);
    lfrfid_worker_stop_thread(worker);
    lfrfid_worker_free(worker);
    protocol_dict_free(dict);
    return matched;
}

static bool tumoflow_wait_ibutton_trigger(TumoFlowApp* app) {
    TumoFlowTriggerFlag flag = {0};
    iButtonProtocols* protocols = ibutton_protocols_alloc();
    iButtonKey* key = ibutton_key_alloc(ibutton_protocols_get_max_data_size(protocols));
    iButtonWorker* worker = ibutton_worker_alloc(protocols);
    ibutton_worker_start_thread(worker);
    ibutton_worker_read_set_callback(worker, tumoflow_ibutton_trigger_callback, &flag);
    ibutton_worker_read_start(worker, key);
    const bool matched =
        tumoflow_wait_trigger_flag(app, &flag.matched, TUMOFLOW_MAX_RUNTIME_MS);
    ibutton_worker_stop(worker);
    ibutton_worker_stop_thread(worker);
    ibutton_worker_free(worker);
    ibutton_key_free(key);
    ibutton_protocols_free(protocols);
    return matched;
}

static bool tumoflow_wait_ir_trigger(TumoFlowApp* app, const TumoFlowPlan* plan) {
    TumoFlowIrReference reference;
    char error[TUMOFLOW_STATUS_SIZE] = {0};
    if(!tumoflow_load_ir_reference(
           app->storage,
           plan->trigger_path,
           plan->trigger_signal,
           true,
           &reference,
           error,
           sizeof(error))) {
        return false;
    }

    TumoFlowIrTriggerContext trigger = {
        .matched = false,
        .expected = reference.message,
    };
    InfraredWorker* worker = infrared_worker_alloc();
    infrared_worker_rx_set_received_signal_callback(
        worker, tumoflow_ir_trigger_callback, &trigger);
    infrared_worker_rx_enable_signal_decoding(worker, true);
    infrared_worker_rx_start(worker);
    const bool matched =
        tumoflow_wait_trigger_flag(app, &trigger.matched, TUMOFLOW_MAX_RUNTIME_MS);
    infrared_worker_rx_set_received_signal_callback(worker, NULL, NULL);
    infrared_worker_rx_stop(worker);
    infrared_worker_free(worker);
    tumoflow_ir_reference_clear(&reference);
    return matched;
}

static bool tumoflow_wait_subghz_trigger(TumoFlowApp* app, const TumoFlowPlan* plan) {
    TumoFlowSubGhzReference reference;
    char error[TUMOFLOW_STATUS_SIZE] = {0};
    if(!tumoflow_load_subghz_reference(
           app->storage,
           plan->trigger_path,
           false,
           &reference,
           error,
           sizeof(error))) {
        return false;
    }

    bool matched = false;
    bool broker_acquired = false;
    bool devices_initialized = false;
    bool device_ready = false;
    bool rx_started = false;
    SubGhzRadioBroker* broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    SubGhzRadioBrokerLease lease = {0};
    const SubGhzDevice* device = NULL;
    SubGhzEnvironment* environment = NULL;
    SubGhzReceiver* receiver = NULL;
    SubGhzWorker* worker = NULL;
    TumoFlowSubGhzTriggerContext trigger = {
        .matched = false,
        .expected = reference,
        .format = NULL,
        .preset =
            {
                .name = NULL,
                .frequency = reference.frequency,
                .data = NULL,
                .data_size = 0U,
            },
    };

    do {
        if(!subghz_radio_broker_acquire(
               broker,
               TUMOFLOW_SUBGHZ_OWNER,
               TUMOFLOW_SUBGHZ_ACQUIRE_TIMEOUT_MS,
               &lease)) {
            break;
        }
        broker_acquired = true;
        subghz_devices_init();
        devices_initialized = true;
        device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        if(device == NULL || !subghz_devices_is_frequency_valid(device, reference.frequency)) {
            break;
        }
        device_ready = true;
        subghz_radio_broker_set_selected_device(
            broker, &lease, SubGhzRadioBrokerDeviceInternal);
        subghz_radio_broker_set_state(
            broker, &lease, SubGhzRadioBrokerStateInitialized);
        subghz_devices_reset(device);
        subghz_devices_load_preset(device, reference.preset, NULL);
        if(subghz_devices_set_frequency(device, reference.frequency) == 0U) break;

        environment = subghz_environment_alloc();
        subghz_environment_set_protocol_registry(
            environment, (void*)&subghz_protocol_registry);
        receiver = subghz_receiver_alloc_init(environment);
        worker = subghz_worker_alloc();
        trigger.format = flipper_format_string_alloc();
        trigger.preset.name = furi_string_alloc_set_str(reference.preset_name);
        subghz_receiver_set_filter(receiver, SubGhzProtocolFlag_Decodable);
        subghz_receiver_set_rx_callback(
            receiver, tumoflow_subghz_trigger_callback, &trigger);
        subghz_worker_set_overrun_callback(
            worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
        subghz_worker_set_pair_callback(
            worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
        subghz_worker_set_context(worker, receiver);

        subghz_devices_flush_rx(device);
        subghz_devices_set_rx(device);
        subghz_devices_start_async_rx(device, subghz_worker_rx_callback, worker);
        subghz_worker_start(worker);
        rx_started = true;
        subghz_radio_broker_set_state(
            broker, &lease, SubGhzRadioBrokerStateAsyncRx);
        matched = tumoflow_wait_trigger_flag(
            app, &trigger.matched, TUMOFLOW_MAX_RUNTIME_MS);
    } while(false);

    if(broker_acquired) {
        subghz_radio_broker_set_state(
            broker, &lease, SubGhzRadioBrokerStateCleaningUp);
    }
    if(rx_started) {
        subghz_worker_stop(worker);
        subghz_devices_stop_async_rx(device);
    }
    if(worker) subghz_worker_free(worker);
    if(receiver) subghz_receiver_free(receiver);
    if(trigger.preset.name) furi_string_free(trigger.preset.name);
    if(trigger.format) flipper_format_free(trigger.format);
    if(environment) subghz_environment_free(environment);
    if(device_ready) {
        subghz_devices_idle(device);
        subghz_devices_sleep(device);
        subghz_devices_end(device);
    }
    if(devices_initialized) subghz_devices_deinit();
    if(broker_acquired) subghz_radio_broker_release(broker, &lease);
    furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
    return matched;
}

static bool tumoflow_wait_trigger(TumoFlowApp* app, const TumoFlowPlan* plan) {
    tumoflow_set_model(
        app,
        TumoFlowModeRun,
        tumoflow_trigger_name(plan->trigger),
        "Waiting / Back cancels",
        0U,
        plan->step_count);
    switch(plan->trigger) {
    case TumoFlowTriggerManual:
        return true;
    case TumoFlowTriggerCountdown:
        return tumoflow_wait_delay(app, plan->trigger_delay_ms);
    case TumoFlowTriggerIr:
        return tumoflow_wait_ir_trigger(app, plan);
    case TumoFlowTriggerSubGhz:
        return tumoflow_wait_subghz_trigger(app, plan);
    case TumoFlowTriggerNfc:
        return tumoflow_wait_nfc_trigger(app);
    case TumoFlowTriggerLfRfid:
        return tumoflow_wait_lfrfid_trigger(app);
    case TumoFlowTriggerIButton:
        return tumoflow_wait_ibutton_trigger(app);
    default:
        return false;
    }
}

static bool tumoflow_send_bridge(TumoFlowApp* app, const char* command, const char* payload) {
    const uint32_t request_id = app->request_id_next++;
    bool sent = bt_app_bridge_send_text_v2(app->bt, TUMOFLOW_APP_ID, command, request_id, 0, payload);
    if(!sent) {
        sent = bt_app_bridge_send_text(app->bt, TUMOFLOW_APP_ID, command, payload);
    }
    return sent;
}

static bool tumoflow_run_gpio_pulse(TumoFlowApp* app, const TumoFlowStep* step) {
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeOutputPushPull);
    for(uint32_t i = 0; i < step->a; i++) {
        furi_hal_gpio_write(&gpio_ext_pc0, true);
        furi_delay_us(step->b);
        furi_hal_gpio_write(&gpio_ext_pc0, false);
        furi_delay_us(step->c);
        InputEvent input;
        if(furi_message_queue_get(app->queue, &input, 0U) == FuriStatusOk &&
           input.type == InputTypeShort && input.key == InputKeyBack) {
            app->cancel_requested = true;
            break;
        }
    }
    furi_hal_gpio_write(&gpio_ext_pc0, false);
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog);
    return !app->cancel_requested;
}

static FuriHalInfraredTxGetDataState
    tumoflow_ir_tx_callback(void* context, uint32_t* duration, bool* level) {
    TumoFlowIrTx* tx = context;
    *level = true;
    *duration = tx->remaining_us;
    tx->remaining_us = 0;
    return FuriHalInfraredTxGetDataStateLastDone;
}

static void tumoflow_ir_done_callback(void* context) {
    bool* done = context;
    *done = true;
}

static bool tumoflow_run_ir_burst(TumoFlowApp* app, const TumoFlowStep* step) {
    if(furi_hal_infrared_is_busy()) {
        return false;
    }

    volatile bool done = false;
    TumoFlowIrTx tx = {
        .remaining_us = step->b * 1000U,
    };

    const FuriHalInfraredTxPin output = furi_hal_infrared_detect_tx_output();
    furi_hal_infrared_set_tx_output(output);
    furi_hal_infrared_async_tx_set_data_isr_callback(tumoflow_ir_tx_callback, &tx);
    furi_hal_infrared_async_tx_set_signal_sent_isr_callback(
        tumoflow_ir_done_callback, (void*)&done);
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

static bool tumoflow_run_ir_file(TumoFlowApp* app, const TumoFlowStep* step) {
    TumoFlowIrReference reference;
    char error[TUMOFLOW_STATUS_SIZE] = {0};
    if(!tumoflow_load_ir_reference(
           app->storage,
           step->value,
           step->arg,
           false,
           &reference,
           error,
           sizeof(error))) {
        return false;
    }

    if(reference.raw) {
        infrared_send_raw_ext(
            reference.timings,
            reference.timings_count,
            true,
            reference.frequency,
            reference.duty_cycle);
    } else {
        infrared_send(&reference.message, 1);
    }
    tumoflow_ir_reference_clear(&reference);
    return true;
}

static bool tumoflow_run_subghz_file(TumoFlowApp* app, const TumoFlowStep* step) {
    TumoFlowSubGhzReference reference;
    char error[TUMOFLOW_STATUS_SIZE] = {0};
    if(!tumoflow_load_subghz_reference(
           app->storage,
           step->value,
           true,
           &reference,
           error,
           sizeof(error))) {
        return false;
    }

    bool success = false;
    bool broker_acquired = false;
    bool devices_initialized = false;
    bool device_ready = false;
    bool tx_started = false;
    bool charge_suppressed = false;
    SubGhzRadioBroker* broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    SubGhzRadioBrokerLease lease = {0};
    const SubGhzDevice* device = NULL;
    SubGhzEnvironment* environment = NULL;
    SubGhzTransmitter* transmitter = NULL;
    FlipperFormat* format = NULL;

    do {
        if(!subghz_radio_broker_acquire(
               broker,
               TUMOFLOW_SUBGHZ_OWNER,
               TUMOFLOW_SUBGHZ_ACQUIRE_TIMEOUT_MS,
               &lease)) {
            break;
        }
        broker_acquired = true;
        subghz_devices_init();
        devices_initialized = true;
        device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        if(device == NULL || !subghz_devices_is_frequency_valid(device, reference.frequency)) {
            break;
        }
        device_ready = true;
        subghz_radio_broker_set_selected_device(
            broker, &lease, SubGhzRadioBrokerDeviceInternal);
        subghz_radio_broker_set_state(
            broker, &lease, SubGhzRadioBrokerStateInitialized);
        subghz_devices_reset(device);
        subghz_devices_load_preset(device, reference.preset, NULL);
        if(subghz_devices_set_frequency(device, reference.frequency) == 0U) break;

        format = flipper_format_string_alloc();
        if(stream_load_from_file(
               flipper_format_get_raw_stream(format), app->storage, step->value) == 0U) {
            break;
        }
        uint32_t repeat = 1U;
        if(!flipper_format_rewind(format) ||
           !flipper_format_insert_or_update_uint32(format, "Repeat", &repeat, 1U) ||
           !flipper_format_rewind(format)) {
            break;
        }
        environment = subghz_environment_alloc();
        subghz_environment_set_protocol_registry(
            environment, (void*)&subghz_protocol_registry);
        transmitter = subghz_transmitter_alloc_init(environment, reference.protocol);
        if(transmitter == NULL ||
           subghz_transmitter_deserialize(transmitter, format) != SubGhzProtocolStatusOk) {
            break;
        }
        if(!subghz_devices_set_tx(device)) break;
        subghz_radio_broker_set_state(broker, &lease, SubGhzRadioBrokerStateTx);

        furi_hal_power_suppress_charge_enter();
        charge_suppressed = true;
        if(!subghz_devices_start_async_tx(device, subghz_transmitter_yield, transmitter)) break;
        tx_started = true;
        subghz_radio_broker_set_state(
            broker, &lease, SubGhzRadioBrokerStateAsyncTx);

        const uint32_t started = furi_get_tick();
        while(!subghz_devices_is_async_complete_tx(device)) {
            InputEvent input;
            if(furi_message_queue_get(
                   app->queue, &input, furi_ms_to_ticks(25U)) == FuriStatusOk &&
               input.type == InputTypeShort && input.key == InputKeyBack) {
                app->cancel_requested = true;
                break;
            }
            if((furi_get_tick() - started) >=
               furi_ms_to_ticks(TUMOFLOW_SUBGHZ_TX_TIMEOUT_MS)) {
                break;
            }
        }
        success =
            subghz_devices_is_async_complete_tx(device) && !app->cancel_requested;
    } while(false);

    if(broker_acquired) {
        subghz_radio_broker_set_state(
            broker, &lease, SubGhzRadioBrokerStateCleaningUp);
    }
    if(tx_started) subghz_devices_stop_async_tx(device);
    if(transmitter) {
        subghz_transmitter_stop(transmitter);
        subghz_transmitter_free(transmitter);
    }
    if(format) flipper_format_free(format);
    if(environment) subghz_environment_free(environment);
    if(charge_suppressed) furi_hal_power_suppress_charge_exit();
    if(device_ready) {
        subghz_devices_idle(device);
        subghz_devices_sleep(device);
        subghz_devices_end(device);
    }
    if(devices_initialized) subghz_devices_deinit();
    if(broker_acquired) subghz_radio_broker_release(broker, &lease);
    furi_record_close(RECORD_SUBGHZ_RADIO_BROKER);
    return success;
}

static const char* tumoflow_step_name(TumoFlowStepType type) {
    switch(type) {
    case TumoFlowStepLabel:
        return "label";
    case TumoFlowStepLog:
        return "log";
    case TumoFlowStepDelay:
        return "delay";
    case TumoFlowStepPrompt:
        return "prompt";
    case TumoFlowStepBridge:
        return "bridge";
    case TumoFlowStepGpioPulse:
        return "gpio_pulse";
    case TumoFlowStepIrBurst:
        return "ir_burst";
    case TumoFlowStepIrFile:
        return "ir_file";
    case TumoFlowStepSubGhzFile:
        return "subghz_file";
    case TumoFlowStepBranchOk:
        return "branch_ok";
    case TumoFlowStepBranchError:
        return "branch_error";
    case TumoFlowStepGoto:
        return "goto";
    default:
        return "unknown";
    }
}

static void tumoflow_execute_plan(
    TumoFlowApp* app,
    const TumoFlowPlan* plan,
    TumoFlowRunMode run_mode) {
    const bool dry_run = run_mode != TumoFlowRunModeExecute;
    const char* mode_name = run_mode == TumoFlowRunModeValidate ? "validate" :
                            run_mode == TumoFlowRunModeDryRun   ? "dry_run" :
                                                                    "run";
    const bool log_opened = tumoflow_open_log(app, mode_name);
    app->cancel_requested = false;

    if(run_mode == TumoFlowRunModeValidate) {
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            "Valid",
            log_opened ? app->log_path : plan->name,
            0,
            plan->step_count);
        if(log_opened) {
            tumoflow_log_step(app, plan, mode_name, NULL, 0, "valid", plan->path);
        }
        tumoflow_close_log(app);
        notification_message(app->notification, &tumoflow_sequence_ok);
        return;
    }

    if(!dry_run && plan->emission_count > 0U) {
        char confirmation[TUMOFLOW_STATUS_SIZE];
        snprintf(
            confirmation,
            sizeof(confirmation),
            "%u output actions",
            (unsigned)plan->emission_count);
        tumoflow_set_model(
            app,
            TumoFlowModeRun,
            confirmation,
            "OK Arm / Back cancel",
            0U,
            plan->step_count);
        if(!tumoflow_wait_ok_or_back(app)) {
            tumoflow_log_step(
                app, plan, mode_name, NULL, 0U, "cancelled", "before arm");
            tumoflow_close_log(app);
            tumoflow_set_model(
                app, TumoFlowModeBrowse, "Cancelled", plan->name, 0U, 0U);
            notification_message(app->notification, &tumoflow_sequence_error);
            return;
        }
    }

    if(!dry_run && !tumoflow_wait_trigger(app, plan)) {
        tumoflow_log_step(
            app,
            plan,
            mode_name,
            NULL,
            0U,
            app->cancel_requested ? "cancelled" : "timeout",
            tumoflow_trigger_name(plan->trigger));
        tumoflow_close_log(app);
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            app->cancel_requested ? "Cancelled" : "Trigger timeout",
            plan->name,
            0U,
            0U);
        notification_message(app->notification, &tumoflow_sequence_error);
        return;
    }
    if(!dry_run) {
        tumoflow_log_step(
            app,
            plan,
            mode_name,
            NULL,
            0U,
            "triggered",
            tumoflow_trigger_name(plan->trigger));
    }

    bool last_ok = true;
    bool stopped_on_error = false;
    bool runtime_exceeded = false;
    uint8_t executed = 0;
    int16_t pc = 0;
    const uint32_t run_started = furi_get_tick();

    while((pc >= 0) && (pc < plan->step_count) && (executed < TUMOFLOW_MAX_EXECUTED_STEPS)) {
        if(!dry_run &&
           (furi_get_tick() - run_started) >= furi_ms_to_ticks(TUMOFLOW_MAX_RUNTIME_MS)) {
            runtime_exceeded = true;
            stopped_on_error = true;
            break;
        }
        const TumoFlowStep* step = &plan->steps[pc];
        executed++;

        char status[TUMOFLOW_STATUS_SIZE];
        snprintf(
            status,
            sizeof(status),
            "%s line %lu",
            tumoflow_step_name(step->type),
            (unsigned long)step->line_no);
        tumoflow_set_model(
            app, TumoFlowModeRun, status, step->value, executed, plan->step_count);

        bool ok = true;
        const char* result = "ok";
        const char* detail = step->value;
        int16_t next_pc = pc + 1;

        switch(step->type) {
        case TumoFlowStepLabel:
            break;
        case TumoFlowStepLog:
            break;
        case TumoFlowStepDelay:
            if(!dry_run) {
                ok = tumoflow_wait_delay(app, step->a);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "cancelled";
            break;
        case TumoFlowStepPrompt:
            if(!dry_run) {
                tumoflow_set_model(
                    app, TumoFlowModeRun, "prompt", step->value, executed, plan->step_count);
                ok = tumoflow_wait_ok_or_back(app);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "cancelled";
            break;
        case TumoFlowStepBridge:
            if(!dry_run) {
                ok = tumoflow_send_bridge(app, step->arg, step->value);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "send_failed";
            detail = step->arg;
            break;
        case TumoFlowStepGpioPulse:
            if(!dry_run) {
                ok = tumoflow_run_gpio_pulse(app, step);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "cancelled";
            break;
        case TumoFlowStepIrBurst:
            if(!dry_run) {
                ok = tumoflow_run_ir_burst(app, step);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "ir_failed";
            break;
        case TumoFlowStepIrFile:
            if(!dry_run) {
                ok = tumoflow_run_ir_file(app, step);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "ir_failed";
            break;
        case TumoFlowStepSubGhzFile:
            if(!dry_run) {
                ok = tumoflow_run_subghz_file(app, step);
            }
            result = ok ? (dry_run ? "dry" : "ok") : "subghz_failed";
            break;
        case TumoFlowStepBranchOk:
            if(last_ok) {
                next_pc = tumoflow_find_label(plan, step->value);
                result = "taken";
            } else {
                result = "skipped";
            }
            break;
        case TumoFlowStepBranchError:
            if(!last_ok) {
                next_pc = tumoflow_find_label(plan, step->value);
                result = "taken";
            } else {
                result = "skipped";
            }
            break;
        case TumoFlowStepGoto:
            next_pc = tumoflow_find_label(plan, step->value);
            result = "taken";
            break;
        default:
            ok = false;
            result = "unsupported";
            break;
        }

        tumoflow_log_step(app, plan, mode_name, step, executed, result, detail);
        if(app->cancel_requested) break;
        last_ok = ok;
        if(!ok && !plan->continue_on_error) {
            stopped_on_error = true;
            break;
        }
        pc = next_pc;
    }

    if(executed >= TUMOFLOW_MAX_EXECUTED_STEPS) {
        stopped_on_error = true;
        tumoflow_log_step(app, plan, mode_name, NULL, executed, "error", "execution limit");
    } else if(runtime_exceeded) {
        tumoflow_log_step(app, plan, mode_name, NULL, executed, "error", "runtime limit");
    }

    tumoflow_close_log(app);
    if(app->cancel_requested) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "Cancelled", plan->name, 0, 0);
        notification_message(app->notification, &tumoflow_sequence_error);
    } else if(stopped_on_error) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "Stopped on error", plan->name, 0, 0);
        notification_message(app->notification, &tumoflow_sequence_error);
    } else {
        tumoflow_set_model(app, TumoFlowModeBrowse, dry_run ? "Dry-run done" : "Done", plan->name, 0, 0);
        notification_message(app->notification, &tumoflow_sequence_ok);
    }
}

static void tumoflow_run_selected(TumoFlowApp* app, TumoFlowRunMode run_mode) {
    TumoFlowFile workflow;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->workflow_count == 0U) {
        furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
        tumoflow_set_model(
            app, TumoFlowModeBrowse, "No workflows", "Add .tflow files", 0, 0);
        return;
    }
    workflow = app->workflows[app->selected];
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);

    TumoFlowPlan* plan = malloc(sizeof(TumoFlowPlan));
    if(!plan) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "No memory", workflow.name, 0, 0);
        return;
    }

    if(!tumoflow_load_plan(app, &workflow, plan)) {
        char detail[TUMOFLOW_STATUS_SIZE];
        snprintf(
            detail,
            sizeof(detail),
            "%u parse, %u validation",
            (unsigned)plan->parse_errors,
            (unsigned)plan->validation_errors);
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            plan->first_error[0] ? plan->first_error : "Load failed",
            detail,
            0,
            0);
        notification_message(app->notification, &tumoflow_sequence_error);
        free(plan);
        return;
    }

    tumoflow_execute_plan(app, plan, run_mode);
    free(plan);
}

static bool tumoflow_get_selected(TumoFlowApp* app, TumoFlowFile* workflow) {
    bool found = false;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->workflow_count > 0U) {
        *workflow = app->workflows[app->selected];
        found = true;
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    return found;
}

static void tumoflow_refresh_selected(TumoFlowApp* app) {
    TumoFlowFile workflow;
    if(!tumoflow_get_selected(app, &workflow)) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "Ready", "No workflows", 0, 0);
        return;
    }

    TumoFlowPlan* plan = malloc(sizeof(TumoFlowPlan));
    if(!plan) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "No memory", workflow.name, 0, 0);
        return;
    }

    const bool valid = tumoflow_load_plan(app, &workflow, plan);
    char detail[TUMOFLOW_STATUS_SIZE];
    if(valid) {
        snprintf(
            detail,
            sizeof(detail),
            "%u actions, %u outputs",
            (unsigned)plan->step_count,
            (unsigned)plan->emission_count);
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            tumoflow_trigger_name(plan->trigger),
            detail,
            0,
            plan->step_count);
    } else {
        snprintf(
            detail,
            sizeof(detail),
            "%u parse, %u validation",
            (unsigned)plan->parse_errors,
            (unsigned)plan->validation_errors);
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            plan->first_error[0] ? plan->first_error : "Invalid workflow",
            detail,
            0,
            plan->step_count);
    }
    free(plan);
}

static void tumoflow_fill_preview(TumoFlowApp* app, uint8_t requested_page) {
    TumoFlowFile workflow;
    if(!tumoflow_get_selected(app, &workflow)) return;

    TumoFlowPlan* plan = malloc(sizeof(TumoFlowPlan));
    if(!plan) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "No memory", workflow.name, 0, 0);
        return;
    }
    if(!tumoflow_load_plan(app, &workflow, plan)) {
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            plan->first_error[0] ? plan->first_error : "Invalid workflow",
            workflow.name,
            0,
            plan->step_count);
        free(plan);
        return;
    }

    const uint8_t page_count = (plan->step_count + 2U) / 3U;
    const uint8_t page = page_count > 0U ? requested_page % page_count : 0U;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->mode = TumoFlowModePreview;
    app->preview_page = page;
    app->total_steps = plan->step_count;
    strlcpy(app->status, tumoflow_trigger_name(plan->trigger), sizeof(app->status));
    strlcpy(app->detail, workflow.name, sizeof(app->detail));
    for(uint8_t row = 0U; row < 3U; row++) {
        const uint8_t index = page * 3U + row;
        if(index < plan->step_count) {
            const TumoFlowStep* step = &plan->steps[index];
            snprintf(
                app->preview[row],
                sizeof(app->preview[row]),
                "%u %.11s %.10s",
                (unsigned)(index + 1U),
                tumoflow_step_name(step->type),
                step->value);
        } else {
            app->preview[row][0] = '\0';
        }
    }
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    view_port_update(app->view_port);
    free(plan);
}

static void tumoflow_import_selected(TumoFlowApp* app) {
    TumoFlowFile workflow;
    if(!tumoflow_get_selected(app, &workflow)) return;
    if(workflow.source == TumoFlowSourceNative) {
        tumoflow_run_selected(app, TumoFlowRunModeDryRun);
        return;
    }

    char timestamp[32];
    char destination[TUMOFLOW_PATH_SIZE];
    tumoflow_format_filename_timestamp(timestamp, sizeof(timestamp));
    snprintf(
        destination,
        sizeof(destination),
        TUMOFLOW_WORKFLOWS_DIR "/import_%s.tflow",
        timestamp);
    if(storage_common_copy(app->storage, workflow.path, destination) != FSE_OK) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "Import failed", workflow.name, 0, 0);
        notification_message(app->notification, &tumoflow_sequence_error);
        return;
    }

    tumoflow_scan_workflows(app);
    tumoflow_set_model(
        app, TumoFlowModeBrowse, "Imported", "Original kept", 0, 0);
    notification_message(app->notification, &tumoflow_sequence_ok);
}

static bool tumoflow_write_trigger_line(
    File* file,
    TumoFlowTriggerType trigger,
    uint32_t delay_ms) {
    char line[64];
    switch(trigger) {
    case TumoFlowTriggerManual:
        strlcpy(line, "trigger manual\n", sizeof(line));
        break;
    case TumoFlowTriggerCountdown:
        snprintf(
            line, sizeof(line), "trigger countdown %lu\n", (unsigned long)delay_ms);
        break;
    case TumoFlowTriggerNfc:
        strlcpy(line, "trigger nfc\n", sizeof(line));
        break;
    case TumoFlowTriggerLfRfid:
        strlcpy(line, "trigger lf_rfid\n", sizeof(line));
        break;
    case TumoFlowTriggerIButton:
        strlcpy(line, "trigger ibutton\n", sizeof(line));
        break;
    default:
        return false;
    }
    return tumoflow_write(file, line);
}

static bool tumoflow_update_native_trigger(
    TumoFlowApp* app,
    const TumoFlowFile* workflow,
    TumoFlowTriggerType trigger,
    uint32_t delay_ms) {
    if(workflow->source != TumoFlowSourceNative) return false;

    File* input = storage_file_alloc(app->storage);
    File* output = storage_file_alloc(app->storage);
    bool success = false;
    bool replaced = false;
    bool input_open = false;
    bool output_open = false;
    storage_common_remove(app->storage, TUMOFLOW_EDIT_TEMP_PATH);
    storage_common_remove(app->storage, TUMOFLOW_EDIT_BACKUP_PATH);

    do {
        if(!storage_file_open(
               input, workflow->path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }
        input_open = true;
        if(!storage_file_open(
               output, TUMOFLOW_EDIT_TEMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            break;
        }
        output_open = true;

        char line[TUMOFLOW_LINE_SIZE];
        size_t line_length = 0U;
        char chunk[64];
        size_t read_count = 0U;
        bool invalid = false;
        while((read_count = storage_file_read(input, chunk, sizeof(chunk))) > 0U) {
            for(size_t index = 0U; index < read_count; index++) {
                const char character = chunk[index];
                if(character == '\r') continue;
                if(character == '\n') {
                    line[line_length] = '\0';
                    char trimmed[TUMOFLOW_LINE_SIZE];
                    strlcpy(trimmed, line, sizeof(trimmed));
                    if(!replaced &&
                       tumoflow_value_after_key(tumoflow_trim(trimmed), "trigger") !=
                           NULL) {
                        if(!tumoflow_write_trigger_line(output, trigger, delay_ms)) {
                            invalid = true;
                            break;
                        }
                        replaced = true;
                    } else if(!tumoflow_write(output, line) ||
                              !tumoflow_write(output, "\n")) {
                        invalid = true;
                        break;
                    }
                    line_length = 0U;
                } else if(line_length + 1U < sizeof(line)) {
                    line[line_length++] = character;
                } else {
                    invalid = true;
                    break;
                }
            }
            if(invalid) break;
        }
        if(invalid) break;
        if(line_length > 0U) {
            line[line_length] = '\0';
            char trimmed[TUMOFLOW_LINE_SIZE];
            strlcpy(trimmed, line, sizeof(trimmed));
            if(!replaced &&
               tumoflow_value_after_key(tumoflow_trim(trimmed), "trigger") != NULL) {
                if(!tumoflow_write_trigger_line(output, trigger, delay_ms)) break;
                replaced = true;
            } else if(!tumoflow_write(output, line) || !tumoflow_write(output, "\n")) {
                break;
            }
        }
        if(!replaced && !tumoflow_write_trigger_line(output, trigger, delay_ms)) break;
        storage_file_sync(output);
        storage_file_close(output);
        output_open = false;
        storage_file_close(input);
        input_open = false;

        if(storage_common_rename(
               app->storage, workflow->path, TUMOFLOW_EDIT_BACKUP_PATH) != FSE_OK) {
            break;
        }
        if(storage_common_rename(
               app->storage, TUMOFLOW_EDIT_TEMP_PATH, workflow->path) != FSE_OK) {
            storage_common_rename(
                app->storage, TUMOFLOW_EDIT_BACKUP_PATH, workflow->path);
            break;
        }
        storage_common_remove(app->storage, TUMOFLOW_EDIT_BACKUP_PATH);
        success = true;
    } while(false);

    if(output_open) storage_file_close(output);
    if(input_open) storage_file_close(input);
    storage_file_free(output);
    storage_file_free(input);
    if(!success) storage_common_remove(app->storage, TUMOFLOW_EDIT_TEMP_PATH);
    return success;
}

static void tumoflow_open_trigger_editor(TumoFlowApp* app) {
    TumoFlowFile workflow;
    if(!tumoflow_get_selected(app, &workflow)) return;
    if(workflow.source != TumoFlowSourceNative) {
        tumoflow_set_model(
            app, TumoFlowModeBrowse, "Import first", "Hold OK keeps original", 0U, 0U);
        return;
    }

    TumoFlowPlan* plan = malloc(sizeof(TumoFlowPlan));
    if(plan == NULL) {
        tumoflow_set_model(app, TumoFlowModeBrowse, "No memory", workflow.name, 0U, 0U);
        return;
    }
    if(!tumoflow_load_plan(app, &workflow, plan)) {
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            plan->first_error[0] ? plan->first_error : "Invalid workflow",
            workflow.name,
            0U,
            plan->step_count);
        free(plan);
        return;
    }
    if(plan->trigger == TumoFlowTriggerIr ||
       plan->trigger == TumoFlowTriggerSubGhz) {
        tumoflow_set_model(
            app,
            TumoFlowModeBrowse,
            "Reference trigger locked",
            "Edit path in .tflow",
            0U,
            plan->step_count);
        free(plan);
        return;
    }

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->mode = TumoFlowModeEdit;
    app->edit_trigger = plan->trigger;
    app->edit_delay_ms =
        plan->trigger_delay_ms > 0U ? plan->trigger_delay_ms : 5000U;
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    view_port_update(app->view_port);
    free(plan);
}

static void tumoflow_cycle_edit_trigger(TumoFlowApp* app, int8_t direction) {
    static const TumoFlowTriggerType safe_triggers[] = {
        TumoFlowTriggerManual,
        TumoFlowTriggerCountdown,
        TumoFlowTriggerNfc,
        TumoFlowTriggerLfRfid,
        TumoFlowTriggerIButton,
    };
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    size_t index = 0U;
    while(index < COUNT_OF(safe_triggers) &&
          safe_triggers[index] != app->edit_trigger) {
        index++;
    }
    if(index >= COUNT_OF(safe_triggers)) index = 0U;
    if(direction < 0) {
        index = index == 0U ? COUNT_OF(safe_triggers) - 1U : index - 1U;
    } else {
        index = (index + 1U) % COUNT_OF(safe_triggers);
    }
    app->edit_trigger = safe_triggers[index];
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
    view_port_update(app->view_port);
}

static void tumoflow_save_trigger_editor(TumoFlowApp* app) {
    TumoFlowFile workflow;
    if(!tumoflow_get_selected(app, &workflow)) return;
    const bool saved = tumoflow_update_native_trigger(
        app, &workflow, app->edit_trigger, app->edit_delay_ms);
    if(saved) {
        tumoflow_refresh_selected(app);
        notification_message(app->notification, &tumoflow_sequence_ok);
    } else {
        tumoflow_set_model(
            app, TumoFlowModeBrowse, "Save failed", workflow.name, 0U, 0U);
        notification_message(app->notification, &tumoflow_sequence_error);
    }
}

static TumoFlowApp* tumoflow_alloc(void) {
    TumoFlowApp* app = malloc(sizeof(TumoFlowApp));
    furi_check(app);
    memset(app, 0, sizeof(TumoFlowApp));
    app->queue = furi_message_queue_alloc(TUMOFLOW_QUEUE_DEPTH, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->bt = furi_record_open(RECORD_BT);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->request_id_next = 1U;
    strlcpy(app->status, "Ready", sizeof(app->status));
    strlcpy(app->detail, "Loading workflows", sizeof(app->detail));

    tumoflow_ensure_dirs(app);
    tumoflow_ensure_sample(app);
    tumoflow_scan_workflows(app);
    app->onboarding_pending =
        storage_common_stat(app->storage, TUMOFLOW_ONBOARDING_PATH, NULL) != FSE_OK;

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tumoflow_draw_callback, app);
    view_port_input_callback_set(app->view_port, tumoflow_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    if(app->onboarding_pending) {
        tumoflow_open_help(app, 0U);
    } else {
        tumoflow_show_dashboard(app);
    }
    return app;
}

static void tumoflow_free(TumoFlowApp* app) {
    tumoflow_close_log(app);
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

int32_t tumoflow_app(void* context) {
    UNUSED(context);
    TumoFlowApp* app = tumoflow_alloc();

    bool running = true;
    while(running) {
        InputEvent input;
        if(furi_message_queue_get(app->queue, &input, FuriWaitForever) != FuriStatusOk) continue;
        if((input.type != InputTypeShort) && (input.type != InputTypeLong)) continue;

        const bool long_press = input.type == InputTypeLong;

        if(input.key == InputKeyBack) {
            if(app->mode == TumoFlowModeHelp) {
                tumoflow_mark_onboarding_seen(app);
                tumoflow_show_dashboard(app);
            } else if(app->mode == TumoFlowModePreview || app->mode == TumoFlowModeEdit) {
                tumoflow_refresh_selected(app);
            } else if(app->mode == TumoFlowModeBrowse ||
                      app->mode == TumoFlowModeAbout) {
                tumoflow_show_dashboard(app);
            } else {
                running = false;
            }
        } else if(input.key == InputKeyUp) {
            if(app->mode == TumoFlowModeHelp) {
                if(!long_press && app->help_page > 0U) {
                    tumoflow_open_help(app, app->help_page - 1U);
                }
                continue;
            }
            if(app->mode == TumoFlowModeEdit) {
                if(!long_press) tumoflow_cycle_edit_trigger(app, -1);
                continue;
            }
            if(app->mode != TumoFlowModeBrowse || long_press) continue;
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->workflow_count > 0U) {
                app->selected = (app->selected == 0U) ? (app->workflow_count - 1U) :
                                                             (app->selected - 1U);
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            tumoflow_refresh_selected(app);
        } else if(input.key == InputKeyDown) {
            if(app->mode == TumoFlowModeHelp) {
                if(!long_press && app->help_page + 1U < TUMOFLOW_HELP_PAGE_COUNT) {
                    tumoflow_open_help(app, app->help_page + 1U);
                }
                continue;
            }
            if(app->mode == TumoFlowModeEdit) {
                if(!long_press) tumoflow_cycle_edit_trigger(app, 1);
                continue;
            }
            if(app->mode != TumoFlowModeBrowse || long_press) continue;
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->workflow_count > 0U) {
                app->selected = (app->selected + 1U) % app->workflow_count;
            }
            furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
            tumoflow_refresh_selected(app);
        } else if(input.key == InputKeyLeft) {
            if(app->mode == TumoFlowModeDashboard) {
                if(!long_press) tumoflow_open_help(app, 0U);
            } else if(app->mode == TumoFlowModeHelp) {
                if(long_press) continue;
                if(app->help_page > 0U) {
                    tumoflow_open_help(app, app->help_page - 1U);
                } else {
                    tumoflow_mark_onboarding_seen(app);
                    tumoflow_show_dashboard(app);
                }
            } else if(app->mode == TumoFlowModeAbout) {
                if(!long_press) tumoflow_show_dashboard(app);
            } else if(app->mode == TumoFlowModePreview) {
                tumoflow_refresh_selected(app);
            } else if(app->mode == TumoFlowModeEdit) {
                if(long_press && app->edit_trigger == TumoFlowTriggerCountdown) {
                    furi_check(
                        furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
                    app->edit_delay_ms =
                        app->edit_delay_ms > 10000U ? app->edit_delay_ms - 10000U :
                                                     1000U;
                    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
                    view_port_update(app->view_port);
                } else {
                    tumoflow_refresh_selected(app);
                }
            } else if(long_press) {
                tumoflow_open_trigger_editor(app);
            } else if(!long_press) {
                tumoflow_run_selected(app, TumoFlowRunModeValidate);
            }
        } else if(input.key == InputKeyRight) {
            if(app->mode == TumoFlowModeDashboard) {
                if(!long_press) tumoflow_show_about(app);
            } else if(app->mode == TumoFlowModeHelp) {
                if(long_press) continue;
                if(app->help_page + 1U < TUMOFLOW_HELP_PAGE_COUNT) {
                    tumoflow_open_help(app, app->help_page + 1U);
                } else {
                    tumoflow_mark_onboarding_seen(app);
                    tumoflow_show_dashboard(app);
                }
            } else if(app->mode == TumoFlowModeAbout) {
                if(!long_press) tumoflow_show_dashboard(app);
            } else if(app->mode == TumoFlowModePreview) {
                tumoflow_fill_preview(app, app->preview_page + 1U);
            } else if(app->mode == TumoFlowModeEdit) {
                if(app->edit_trigger == TumoFlowTriggerCountdown) {
                    const uint32_t increment = long_press ? 10000U : 1000U;
                    furi_check(
                        furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
                    app->edit_delay_ms =
                        app->edit_delay_ms + increment <= TUMOFLOW_MAX_DELAY_MS ?
                            app->edit_delay_ms + increment :
                            TUMOFLOW_MAX_DELAY_MS;
                    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
                    view_port_update(app->view_port);
                }
            } else if(long_press) {
                tumoflow_run_selected(app, TumoFlowRunModeDryRun);
            } else {
                tumoflow_fill_preview(app, 0U);
            }
        } else if(input.key == InputKeyOk) {
            if(app->mode == TumoFlowModeDashboard) {
                if(!long_press) tumoflow_refresh_selected(app);
            } else if(app->mode == TumoFlowModeHelp) {
                if(long_press) continue;
                if(app->help_page + 1U < TUMOFLOW_HELP_PAGE_COUNT) {
                    tumoflow_open_help(app, app->help_page + 1U);
                } else {
                    tumoflow_mark_onboarding_seen(app);
                    tumoflow_show_dashboard(app);
                }
            } else if(app->mode == TumoFlowModeAbout) {
                if(!long_press) tumoflow_show_dashboard(app);
            } else if(app->mode == TumoFlowModeEdit) {
                if(!long_press) tumoflow_save_trigger_editor(app);
            } else if(long_press && app->mode == TumoFlowModeBrowse) {
                tumoflow_import_selected(app);
            } else {
                tumoflow_run_selected(app, TumoFlowRunModeExecute);
            }
        }
    }

    tumoflow_free(app);
    return 0;
}
