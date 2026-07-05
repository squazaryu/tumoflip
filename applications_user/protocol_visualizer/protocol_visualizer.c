#include <furi.h>
#include <furi_hal_rtc.h>
#include <dialogs/dialogs.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PV_APP_DATA_DIR  EXT_PATH("apps_data/protocol_visualizer")
#define PV_SUMMARY_DIR   EXT_PATH("apps_data/protocol_visualizer/summaries")
#define PV_IR_BASE_DIR   EXT_PATH("infrared")
#define PV_SUB_BASE_DIR  EXT_PATH("subghz")
#define PV_MAX_SAMPLES   4096U
#define PV_LINE_BUF_SIZE 160U
#define PV_PATH_SIZE     128U
#define PV_TEXT_SIZE     64U
#define PV_WAVE_TOP      18
#define PV_WAVE_BOTTOM   47
#define PV_WAVE_BASE     33
#define PV_WAVE_PULSE_Y  24
#define PV_WAVE_GAP_Y    42

typedef enum {
    ProtocolVisualizerViewMenu,
    ProtocolVisualizerViewWave,
    ProtocolVisualizerViewText,
} ProtocolVisualizerView;

typedef enum {
    ProtocolVisualizerMenuOpenIr,
    ProtocolVisualizerMenuOpenSubGhz,
    ProtocolVisualizerMenuExport,
    ProtocolVisualizerMenuAbout,
} ProtocolVisualizerMenu;

typedef enum {
    ProtocolVisualizerKindNone,
    ProtocolVisualizerKindIr,
    ProtocolVisualizerKindSubGhz,
} ProtocolVisualizerKind;

typedef struct {
    File* file;
    uint8_t buffer[128];
    size_t len;
    size_t pos;
    bool eof;
} ProtocolVisualizerLineReader;

typedef struct {
    ProtocolVisualizerKind kind;
    char path[PV_PATH_SIZE];
    char name[PV_TEXT_SIZE];
    char protocol[PV_TEXT_SIZE];
    char preset[PV_TEXT_SIZE];
    char detail1[PV_TEXT_SIZE];
    char detail2[PV_TEXT_SIZE];
    uint32_t frequency;
    uint32_t duty_x1000;
    int32_t samples[PV_MAX_SAMPLES];
    size_t stored_count;
    size_t total_count;
    uint64_t total_us;
    uint32_t min_us;
    uint32_t max_us;
    size_t pulse_count;
    size_t gap_count;
    size_t long_gap_count;
    bool truncated;
    bool decoded_only;
    bool valid;
} ProtocolVisualizerCapture;

typedef struct {
    Gui* gui;
    DialogsApp* dialogs;
    Storage* storage;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;
    View* wave_view;
    FuriString* file_path;
    FuriString* text;
    ProtocolVisualizerCapture capture;
    size_t window_start;
    size_t window_size;
} ProtocolVisualizerApp;

static bool pv_read_line(ProtocolVisualizerLineReader* reader, FuriString* output) {
    furi_string_reset(output);
    bool any = false;

    while(true) {
        if(reader->pos >= reader->len) {
            if(reader->eof) break;

            reader->len = storage_file_read(reader->file, reader->buffer, sizeof(reader->buffer));
            reader->pos = 0;
            if(reader->len == 0) {
                reader->eof = true;
                break;
            }
        }

        char c = (char)reader->buffer[reader->pos++];
        if(c == '\n') {
            any = true;
            break;
        }
        if(c == '\r') continue;

        furi_string_push_back(output, c);
        any = true;
    }

    return any;
}

static const char* pv_skip_space(const char* value) {
    while(*value && isspace((unsigned char)*value)) value++;
    return value;
}

static const char* pv_value_after_key(const char* line, const char* key) {
    const size_t key_len = strlen(key);
    if(strncmp(line, key, key_len) != 0) return NULL;
    if(line[key_len] != ':') return NULL;
    return pv_skip_space(line + key_len + 1);
}

static void pv_copy_value(char* output, size_t output_size, const char* value) {
    strlcpy(output, pv_skip_space(value), output_size);
}

static void pv_capture_reset(ProtocolVisualizerCapture* capture, ProtocolVisualizerKind kind) {
    memset(capture, 0, sizeof(*capture));
    capture->kind = kind;
    capture->min_us = UINT32_MAX;
}

static uint32_t pv_abs_i32(int32_t value) {
    return (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
}

static void pv_capture_append_sample(ProtocolVisualizerCapture* capture, int32_t sample) {
    if(sample == 0) return;

    const uint32_t duration = pv_abs_i32(sample);
    capture->total_count++;
    capture->total_us += duration;
    if(duration < capture->min_us) capture->min_us = duration;
    if(duration > capture->max_us) capture->max_us = duration;
    if(sample > 0) {
        capture->pulse_count++;
    } else {
        capture->gap_count++;
        if(duration >= 10000U) capture->long_gap_count++;
    }

    if(capture->stored_count < PV_MAX_SAMPLES) {
        capture->samples[capture->stored_count++] = sample;
    } else {
        capture->truncated = true;
    }
}

static void pv_parse_sample_list(
    ProtocolVisualizerCapture* capture,
    const char* value,
    bool signed_samples,
    bool alternate_ir_levels) {
    const char* cursor = value;

    while(*cursor) {
        char* end = NULL;
        long parsed = strtol(cursor, &end, 10);
        if(end == cursor) {
            cursor++;
            continue;
        }

        if(parsed > 10000000L) parsed = 10000000L;
        if(parsed < -10000000L) parsed = -10000000L;

        int32_t sample = (int32_t)parsed;
        if(!signed_samples) {
            const int32_t duration = (sample < 0) ? -sample : sample;
            sample = (alternate_ir_levels && (capture->total_count % 2U)) ? -duration : duration;
        }

        pv_capture_append_sample(capture, sample);
        cursor = end;
    }
}

static void pv_capture_add_detail(ProtocolVisualizerCapture* capture, const char* key, const char* value) {
    char line[PV_TEXT_SIZE];
    snprintf(line, sizeof(line), "%s: %s", key, pv_skip_space(value));
    if(capture->detail1[0] == '\0') {
        strlcpy(capture->detail1, line, sizeof(capture->detail1));
    } else if(capture->detail2[0] == '\0') {
        strlcpy(capture->detail2, line, sizeof(capture->detail2));
    }
}

static void pv_capture_set_file(ProtocolVisualizerCapture* capture, const char* path) {
    strlcpy(capture->path, path, sizeof(capture->path));
    const char* name = strrchr(path, '/');
    strlcpy(capture->name, name ? name + 1 : path, sizeof(capture->name));
}

static bool pv_ends_with(const char* text, const char* suffix) {
    const size_t text_len = strlen(text);
    const size_t suffix_len = strlen(suffix);
    return (text_len >= suffix_len) && (strcmp(text + text_len - suffix_len, suffix) == 0);
}

static bool pv_load_ir(ProtocolVisualizerApp* app, const char* path) {
    ProtocolVisualizerCapture* capture = &app->capture;
    pv_capture_reset(capture, ProtocolVisualizerKindIr);
    pv_capture_set_file(capture, path);

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return false;
    }

    ProtocolVisualizerLineReader reader = {.file = file};
    FuriString* line = furi_string_alloc();
    bool current_raw = false;
    bool raw_loaded = false;

    while(pv_read_line(&reader, line)) {
        const char* text = furi_string_get_cstr(line);
        const char* value = NULL;

        if((value = pv_value_after_key(text, "name"))) {
            if(capture->detail1[0] == '\0') pv_capture_add_detail(capture, "Name", value);
            current_raw = false;
        } else if((value = pv_value_after_key(text, "type"))) {
            current_raw = strcmp(value, "raw") == 0;
            if(current_raw) {
                strlcpy(capture->protocol, "IR RAW", sizeof(capture->protocol));
            }
        } else if((value = pv_value_after_key(text, "frequency"))) {
            if(capture->frequency == 0) capture->frequency = strtoul(value, NULL, 10);
        } else if((value = pv_value_after_key(text, "duty_cycle"))) {
            if(capture->duty_x1000 == 0) {
                float duty = strtof(value, NULL);
                if(duty > 0.0f) capture->duty_x1000 = (uint32_t)(duty * 1000.0f + 0.5f);
            }
        } else if((value = pv_value_after_key(text, "protocol"))) {
            if(capture->protocol[0] == '\0') pv_copy_value(capture->protocol, sizeof(capture->protocol), value);
        } else if((value = pv_value_after_key(text, "address"))) {
            pv_capture_add_detail(capture, "Address", value);
        } else if((value = pv_value_after_key(text, "command"))) {
            pv_capture_add_detail(capture, "Command", value);
        } else if((value = pv_value_after_key(text, "data"))) {
            if(current_raw && !raw_loaded) {
                pv_parse_sample_list(capture, value, false, true);
                raw_loaded = capture->total_count > 0;
            }
        }
    }

    furi_string_free(line);
    storage_file_close(file);
    storage_file_free(file);

    if(capture->protocol[0] == '\0') strlcpy(capture->protocol, "IR", sizeof(capture->protocol));
    capture->decoded_only = capture->total_count == 0;
    capture->valid = true;
    return true;
}

static bool pv_load_subghz(ProtocolVisualizerApp* app, const char* path) {
    ProtocolVisualizerCapture* capture = &app->capture;
    pv_capture_reset(capture, ProtocolVisualizerKindSubGhz);
    pv_capture_set_file(capture, path);

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return false;
    }

    ProtocolVisualizerLineReader reader = {.file = file};
    FuriString* line = furi_string_alloc();

    while(pv_read_line(&reader, line)) {
        const char* text = furi_string_get_cstr(line);
        const char* value = NULL;

        if((value = pv_value_after_key(text, "Frequency"))) {
            capture->frequency = strtoul(value, NULL, 10);
        } else if((value = pv_value_after_key(text, "Preset"))) {
            pv_copy_value(capture->preset, sizeof(capture->preset), value);
        } else if((value = pv_value_after_key(text, "Protocol"))) {
            pv_copy_value(capture->protocol, sizeof(capture->protocol), value);
        } else if((value = pv_value_after_key(text, "RAW_Data"))) {
            pv_parse_sample_list(capture, value, true, false);
        } else if((value = pv_value_after_key(text, "Key"))) {
            pv_capture_add_detail(capture, "Key", value);
        } else if((value = pv_value_after_key(text, "Bit"))) {
            pv_capture_add_detail(capture, "Bit", value);
        } else if((value = pv_value_after_key(text, "TE"))) {
            pv_capture_add_detail(capture, "TE", value);
        } else if((value = pv_value_after_key(text, "Te"))) {
            pv_capture_add_detail(capture, "Te", value);
        } else if((value = pv_value_after_key(text, "Serial"))) {
            pv_capture_add_detail(capture, "Serial", value);
        }
    }

    furi_string_free(line);
    storage_file_close(file);
    storage_file_free(file);

    if(capture->protocol[0] == '\0') strlcpy(capture->protocol, "Sub-GHz", sizeof(capture->protocol));
    capture->decoded_only = capture->total_count == 0;
    capture->valid = true;
    return true;
}

static void pv_window_reset(ProtocolVisualizerApp* app) {
    app->window_start = 0;
    app->window_size = app->capture.stored_count < 256U ? app->capture.stored_count : 256U;
    if(app->window_size < 16U) app->window_size = app->capture.stored_count;
}

static void pv_format_frequency(char* output, size_t output_size, uint32_t frequency) {
    if(frequency >= 1000000U) {
        snprintf(
            output,
            output_size,
            "%lu.%03lu MHz",
            (unsigned long)(frequency / 1000000U),
            (unsigned long)((frequency % 1000000U) / 1000U));
    } else if(frequency > 0U) {
        snprintf(output, output_size, "%lu Hz", (unsigned long)frequency);
    } else {
        strlcpy(output, "-", output_size);
    }
}

static void pv_append_summary(ProtocolVisualizerApp* app, FuriString* output) {
    const ProtocolVisualizerCapture* capture = &app->capture;
    char frequency[32];
    pv_format_frequency(frequency, sizeof(frequency), capture->frequency);

    furi_string_cat_printf(output, "Protocol Visualizer\n\n");
    furi_string_cat_printf(output, "File: %s\n", capture->name);
    furi_string_cat_printf(
        output,
        "Type: %s\n",
        capture->kind == ProtocolVisualizerKindIr ? "IR" : "Sub-GHz");
    furi_string_cat_printf(output, "Protocol: %s\n", capture->protocol);
    furi_string_cat_printf(output, "Frequency: %s\n", frequency);
    if(capture->duty_x1000 > 0U) {
        furi_string_cat_printf(
            output,
            "Duty: %lu.%lu%%\n",
            (unsigned long)(capture->duty_x1000 / 10U),
            (unsigned long)(capture->duty_x1000 % 10U));
    }
    if(capture->preset[0]) furi_string_cat_printf(output, "Preset: %s\n", capture->preset);
    if(capture->detail1[0]) furi_string_cat_printf(output, "%s\n", capture->detail1);
    if(capture->detail2[0]) furi_string_cat_printf(output, "%s\n", capture->detail2);

    furi_string_cat_printf(output, "\nTiming\n");
    furi_string_cat_printf(output, "Samples: %u\n", (unsigned)capture->total_count);
    if(capture->stored_count != capture->total_count) {
        furi_string_cat_printf(output, "Stored: %u\n", (unsigned)capture->stored_count);
    }
    furi_string_cat_printf(output, "Pulses/Gaps: %u/%u\n", (unsigned)capture->pulse_count, (unsigned)capture->gap_count);
    furi_string_cat_printf(output, "Long gaps: %u\n", (unsigned)capture->long_gap_count);
    if(capture->total_count > 0U) {
        const uint32_t mean_us = (uint32_t)(capture->total_us / capture->total_count);
        furi_string_cat_printf(
            output,
            "Total: %lu.%03lus\n",
            (unsigned long)(capture->total_us / 1000000ULL),
            (unsigned long)((capture->total_us % 1000000ULL) / 1000ULL));
        furi_string_cat_printf(
            output,
            "Min/Mean/Max: %lu/%lu/%lu us\n",
            (unsigned long)capture->min_us,
            (unsigned long)mean_us,
            (unsigned long)capture->max_us);
    }
    if(capture->decoded_only) {
        furi_string_cat(output, "Timeline: metadata only\n");
    }
    if(capture->truncated) {
        furi_string_cat(output, "Note: timeline truncated in memory\n");
    }
    furi_string_cat_printf(output, "\nPath:\n%s\n", capture->path);
}

static void pv_show_text(ProtocolVisualizerApp* app) {
    furi_string_reset(app->text);
    pv_append_summary(app, app->text);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewText);
}

static bool pv_export_summary(ProtocolVisualizerApp* app) {
    if(!app->capture.valid) return false;

    storage_common_mkdir(app->storage, PV_APP_DATA_DIR);
    storage_common_mkdir(app->storage, PV_SUMMARY_DIR);

    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    char path[PV_PATH_SIZE];
    snprintf(
        path,
        sizeof(path),
        PV_SUMMARY_DIR "/summary_%04u%02u%02u_%02u%02u%02u.txt",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);

    furi_string_reset(app->text);
    pv_append_summary(app, app->text);
    furi_string_cat_printf(app->text, "\nExported:\n%s\n", path);

    File* file = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const char* data = furi_string_get_cstr(app->text);
        const size_t size = strlen(data);
        ok = storage_file_write(file, data, size) == size;
        storage_file_close(file);
    }
    storage_file_free(file);

    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewText);
    return ok;
}

static bool pv_select_and_load(
    ProtocolVisualizerApp* app,
    const char* extension,
    const char* base_path,
    bool (*loader)(ProtocolVisualizerApp*, const char*)) {
    furi_string_set(app->file_path, base_path);

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, extension, NULL);
    options.base_path = base_path;

    bool selected =
        dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &options);
    if(!selected) return false;

    const bool loaded = loader(app, furi_string_get_cstr(app->file_path));
    if(loaded) {
        pv_window_reset(app);
        submenu_set_selected_item(app->submenu, ProtocolVisualizerMenuExport);
        view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewWave);
    } else {
        furi_string_printf(
            app->text,
            "Cannot load file.\n\n%s",
            furi_string_get_cstr(app->file_path));
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
        text_box_set_focus(app->text_box, TextBoxFocusStart);
        view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewText);
    }
    return loaded;
}

static void pv_menu_callback(void* context, uint32_t index) {
    ProtocolVisualizerApp* app = context;

    switch(index) {
    case ProtocolVisualizerMenuOpenIr:
        pv_select_and_load(app, ".ir", PV_IR_BASE_DIR, pv_load_ir);
        break;
    case ProtocolVisualizerMenuOpenSubGhz:
        pv_select_and_load(app, ".sub", PV_SUB_BASE_DIR, pv_load_subghz);
        break;
    case ProtocolVisualizerMenuExport:
        if(app->capture.valid) {
            pv_export_summary(app);
        } else {
            furi_string_set(app->text, "Load a capture first.");
            text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
            view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewText);
        }
        break;
    case ProtocolVisualizerMenuAbout:
        furi_string_set(
            app->text,
            "Protocol Visualizer 0.1\n\n"
            "Receive-only analyzer for saved IR and Sub-GHz captures.\n\n"
            "It renders pulse/gap timing and exports compact summaries. It never replays or transmits captures.");
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
        text_box_set_focus(app->text_box, TextBoxFocusStart);
        view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewText);
        break;
    default:
        break;
    }
}

static void pv_draw_wave(Canvas* canvas, void* context) {
    ProtocolVisualizerApp* app = context;
    const ProtocolVisualizerCapture* capture = &app->capture;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Protocol Visualizer");

    canvas_set_font(canvas, FontSecondary);
    char frequency[24];
    pv_format_frequency(frequency, sizeof(frequency), capture->frequency);
    char line[96];
    snprintf(
        line,
        sizeof(line),
        "%s %s",
        capture->kind == ProtocolVisualizerKindIr ? "IR" : "SUB",
        capture->protocol);
    canvas_draw_str(canvas, 0, 20, line);

    snprintf(
        line,
        sizeof(line),
        "%s  %u samples%s",
        frequency,
        (unsigned)capture->total_count,
        capture->truncated ? "*" : "");
    canvas_draw_str(canvas, 0, 61, line);

    canvas_draw_line(canvas, 0, PV_WAVE_BASE, 127, PV_WAVE_BASE);
    canvas_draw_frame(canvas, 0, PV_WAVE_TOP, 128, PV_WAVE_BOTTOM - PV_WAVE_TOP + 1);

    if(capture->stored_count == 0U) {
        elements_multiline_text_aligned(canvas, 5, 28, AlignLeft, AlignTop, "metadata only\nOK: details");
        return;
    }

    size_t start = app->window_start;
    size_t end = start + app->window_size;
    if(end > capture->stored_count) end = capture->stored_count;
    if(start >= end) return;

    uint64_t window_total = 0;
    for(size_t i = start; i < end; i++) {
        window_total += pv_abs_i32(capture->samples[i]);
    }
    if(window_total == 0U) window_total = 1U;

    uint64_t elapsed = 0;
    int last_y = PV_WAVE_BASE;
    for(size_t i = start; i < end; i++) {
        const uint32_t duration = pv_abs_i32(capture->samples[i]);
        int x1 = (int)((elapsed * 127ULL) / window_total);
        elapsed += duration;
        int x2 = (int)((elapsed * 127ULL) / window_total);
        if(x2 <= x1) x2 = x1 + 1;
        if(x2 > 127) x2 = 127;

        int y = capture->samples[i] > 0 ? PV_WAVE_PULSE_Y : PV_WAVE_GAP_Y;
        if(i != start) canvas_draw_line(canvas, x1, last_y, x1, y);
        canvas_draw_line(canvas, x1, y, x2, y);
        last_y = y;
    }

    elements_button_left(canvas, "<");
    elements_button_center(canvas, "Info");
    elements_button_right(canvas, ">");
}

static bool pv_wave_input(InputEvent* event, void* context) {
    ProtocolVisualizerApp* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    if(event->key == InputKeyOk) {
        pv_show_text(app);
        return true;
    } else if(event->key == InputKeyLeft) {
        if(app->window_start > 0U) {
            const size_t step = app->window_size / 2U ? app->window_size / 2U : 1U;
            app->window_start = app->window_start > step ? app->window_start - step : 0U;
            view_commit_model(app->wave_view, true);
        }
        return true;
    } else if(event->key == InputKeyRight) {
        if(app->window_start + app->window_size < app->capture.stored_count) {
            const size_t step = app->window_size / 2U ? app->window_size / 2U : 1U;
            app->window_start += step;
            if(app->window_start + app->window_size > app->capture.stored_count) {
                app->window_start = app->capture.stored_count - app->window_size;
            }
            view_commit_model(app->wave_view, true);
        }
        return true;
    } else if(event->key == InputKeyUp) {
        if(app->window_size > 32U) {
            app->window_size /= 2U;
            view_commit_model(app->wave_view, true);
        }
        return true;
    } else if(event->key == InputKeyDown) {
        if(app->window_size < app->capture.stored_count) {
            app->window_size *= 2U;
            if(app->window_size > app->capture.stored_count) app->window_size = app->capture.stored_count;
            view_commit_model(app->wave_view, true);
        }
        return true;
    }

    return false;
}

static bool pv_back_callback(void* context) {
    ProtocolVisualizerApp* app = context;
    UNUSED(app);
    view_dispatcher_stop(((ProtocolVisualizerApp*)context)->view_dispatcher);
    return true;
}

static uint32_t pv_wave_previous_callback(void* context) {
    UNUSED(context);
    return ProtocolVisualizerViewMenu;
}

static uint32_t pv_text_previous_callback(void* context) {
    UNUSED(context);
    return ProtocolVisualizerViewMenu;
}

static ProtocolVisualizerApp* pv_app_alloc(void) {
    ProtocolVisualizerApp* app = malloc(sizeof(ProtocolVisualizerApp));
    memset(app, 0, sizeof(*app));

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_box = text_box_alloc();
    app->wave_view = view_alloc();
    app->file_path = furi_string_alloc();
    app->text = furi_string_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, pv_back_callback);

    submenu_set_header(app->submenu, "Protocol Visualizer");
    submenu_add_item(app->submenu, "Open IR .ir", ProtocolVisualizerMenuOpenIr, pv_menu_callback, app);
    submenu_add_item(
        app->submenu, "Open Sub-GHz .sub", ProtocolVisualizerMenuOpenSubGhz, pv_menu_callback, app);
    submenu_add_item(
        app->submenu, "Export summary", ProtocolVisualizerMenuExport, pv_menu_callback, app);
    submenu_add_item(app->submenu, "About", ProtocolVisualizerMenuAbout, pv_menu_callback, app);

    text_box_set_focus(app->text_box, TextBoxFocusStart);
    view_set_context(app->wave_view, app);
    view_set_draw_callback(app->wave_view, pv_draw_wave);
    view_set_input_callback(app->wave_view, pv_wave_input);
    view_set_previous_callback(app->wave_view, pv_wave_previous_callback);
    view_set_previous_callback(text_box_get_view(app->text_box), pv_text_previous_callback);

    view_dispatcher_add_view(
        app->view_dispatcher, ProtocolVisualizerViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(app->view_dispatcher, ProtocolVisualizerViewWave, app->wave_view);
    view_dispatcher_add_view(
        app->view_dispatcher, ProtocolVisualizerViewText, text_box_get_view(app->text_box));
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewMenu);

    return app;
}

static void pv_try_load_context_path(ProtocolVisualizerApp* app, const char* path) {
    bool loaded = false;
    if(pv_ends_with(path, ".ir")) {
        loaded = pv_load_ir(app, path);
    } else if(pv_ends_with(path, ".sub")) {
        loaded = pv_load_subghz(app, path);
    }

    if(loaded) {
        pv_window_reset(app);
        submenu_set_selected_item(app->submenu, ProtocolVisualizerMenuExport);
        view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewWave);
    } else {
        furi_string_printf(app->text, "Cannot load:\n%s", path);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
        text_box_set_focus(app->text_box, TextBoxFocusStart);
        view_dispatcher_switch_to_view(app->view_dispatcher, ProtocolVisualizerViewText);
    }
}

static void pv_app_free(ProtocolVisualizerApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ProtocolVisualizerViewText);
    view_dispatcher_remove_view(app->view_dispatcher, ProtocolVisualizerViewWave);
    view_dispatcher_remove_view(app->view_dispatcher, ProtocolVisualizerViewMenu);

    furi_string_free(app->text);
    furi_string_free(app->file_path);
    view_free(app->wave_view);
    text_box_free(app->text_box);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t protocol_visualizer_app(void* context) {
    ProtocolVisualizerApp* app = pv_app_alloc();
    if(context) {
        pv_try_load_context_path(app, context);
    }
    view_dispatcher_run(app->view_dispatcher);
    pv_app_free(app);
    return 0;
}
