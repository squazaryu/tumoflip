#include "tumoscope_capture.h"
#include "tumoscope_core.h"

#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdarg.h>

#define TAG "TumoScope"

#define TUMOSCOPE_APP_VERSION      "0.2.0"
#define TUMOSCOPE_DATA_DIR         EXT_PATH("apps_data/tumoscope")
#define TUMOSCOPE_CAPTURE_DIR      TUMOSCOPE_DATA_DIR "/captures"
#define TUMOSCOPE_EVENT_QUEUE_SIZE 8U
#define TUMOSCOPE_WAVE_LEFT        12U
#define TUMOSCOPE_WAVE_WIDTH       116U

typedef enum {
    TumoScopeScreenSetup,
    TumoScopeScreenCapture,
    TumoScopeScreenWaveform,
    TumoScopeScreenDecode,
} TumoScopeScreen;

typedef struct {
    TumoScopeDecoder decoder;
    TumoScopeDemo demo;
    uint32_t baud;
    const char* label;
    const char* wiring;
} TumoScopeDecoderProfile;

typedef struct {
    InputEvent input;
} TumoScopeEvent;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notifications;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    TumoScopeCapture* capture;
    uint8_t* samples;

    TumoScopeScreen screen;
    bool running;
    bool exported;
    char status[32];
    char export_path[128];

    uint8_t selected_row;
    uint8_t rate_index;
    uint8_t depth_index;
    uint8_t trigger_index;
    uint8_t decoder_index;

    size_t sample_count;
    size_t trigger_sample;
    size_t wave_offset;
    size_t samples_per_pixel;
    TumoScopeChannelStats channel_stats[TUMOSCOPE_CHANNEL_COUNT];
    TumoScopeDecodeResult decode;
} TumoScopeApp;

static const uint32_t tumoscope_rates[] = {100000U, 250000U, 500000U, 1000000U};
static const size_t tumoscope_depths[] = {1024U, 2048U, 4096U, 8192U};
static const char* const tumoscope_trigger_labels[] = {
    "Auto",
    "PC0 rising",
    "PC0 falling",
    "PC1 rising",
    "PC1 falling",
    "PC3 rising",
    "PC3 falling",
    "PC0 high",
    "PC0 low",
    "PC1 high",
    "PC1 low",
    "PC3 high",
    "PC3 low",
};
static const TumoScopeDecoderProfile tumoscope_decoders[] = {
    {TumoScopeDecoderRaw, TumoScopeDemoNone, 0U, "Live Raw", "PC0 / PC1 / PC3"},
    {TumoScopeDecoderRaw, TumoScopeDemoEdge, 0U, "Demo Edge", "Synthetic / no GPIO"},
    {TumoScopeDecoderUart, TumoScopeDemoUart, 9600U, "Demo UART", "Synthetic / no GPIO"},
    {TumoScopeDecoderI2c, TumoScopeDemoI2c, 0U, "Demo I2C", "Synthetic / no GPIO"},
    {TumoScopeDecoderUart, TumoScopeDemoNone, 9600U, "UART 9.6k", "RX = PC0"},
    {TumoScopeDecoderUart, TumoScopeDemoNone, 19200U, "UART 19.2k", "RX = PC0"},
    {TumoScopeDecoderUart, TumoScopeDemoNone, 38400U, "UART 38.4k", "RX = PC0"},
    {TumoScopeDecoderUart, TumoScopeDemoNone, 57600U, "UART 57.6k", "RX = PC0"},
    {TumoScopeDecoderUart, TumoScopeDemoNone, 115200U, "UART 115.2k", "RX = PC0"},
    {TumoScopeDecoderI2c, TumoScopeDemoNone, 0U, "I2C", "SDA=PC0 SCL=PC1"},
    {TumoScopeDecoderSpiMode0, TumoScopeDemoNone, 0U, "SPI mode 0", "MO=PC0 MI=PC1 CK=PC3"},
    {TumoScopeDecoderOneWire, TumoScopeDemoNone, 0U, "1-Wire", "DATA = PC0"},
};

static const char* tumoscope_rate_label(uint32_t rate) {
    switch(rate) {
    case 100000U:
        return "100 kHz";
    case 250000U:
        return "250 kHz";
    case 500000U:
        return "500 kHz";
    case 1000000U:
        return "1 MHz";
    default:
        return "?";
    }
}

static void tumoscope_draw_selected_row(
    Canvas* canvas,
    uint8_t row,
    uint8_t selected,
    const char* label,
    const char* value) {
    const uint8_t y = 18U + row * 10U;
    if(row == selected) {
        canvas_draw_box(canvas, 0, y - 7U, 128, 9);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 3, y, label);
    canvas_draw_str_aligned(canvas, 125, y, AlignRight, AlignBottom, value);
    canvas_set_color(canvas, ColorBlack);
}

static void tumoscope_draw_setup(Canvas* canvas, const TumoScopeApp* app) {
    const TumoScopeDecoderProfile* profile = &tumoscope_decoders[app->decoder_index];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "TumoScope");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        126,
        9,
        AlignRight,
        AlignBottom,
        profile->demo == TumoScopeDemoNone ? "PC0/1/3 3.3V" : "No GPIO used");

    char depth[16];
    snprintf(depth, sizeof(depth), "%u samples", (unsigned int)tumoscope_depths[app->depth_index]);
    tumoscope_draw_selected_row(
        canvas,
        0U,
        app->selected_row,
        "Rate",
        tumoscope_rate_label(tumoscope_rates[app->rate_index]));
    tumoscope_draw_selected_row(canvas, 1U, app->selected_row, "Depth", depth);
    tumoscope_draw_selected_row(
        canvas,
        2U,
        app->selected_row,
        "Trigger",
        profile->demo == TumoScopeDemoNone ? tumoscope_trigger_labels[app->trigger_index] :
                                             "N/A (demo)");
    tumoscope_draw_selected_row(canvas, 3U, app->selected_row, "Mode", profile->label);
    elements_button_center(canvas, profile->demo == TumoScopeDemoNone ? "Capture" : "Run Demo");
}

static void tumoscope_draw_capture(Canvas* canvas, const TumoScopeApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoScope Capture");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 23, tumoscope_decoders[app->decoder_index].wiring);
    canvas_draw_str(
        canvas,
        2,
        35,
        tumoscope_capture_is_triggered(app->capture) ? "Triggered / post-buffer" :
                                                       "Waiting for trigger");
    char progress[40];
    snprintf(
        progress,
        sizeof(progress),
        "%u / %u samples",
        (unsigned int)tumoscope_capture_progress(app->capture),
        (unsigned int)tumoscope_depths[app->depth_index]);
    canvas_draw_str(canvas, 2, 47, progress);
    canvas_draw_line(canvas, 0, 51, 127, 51);
    elements_button_center(canvas, "Stop");
}

static uint8_t tumoscope_wave_y(uint8_t channel, bool high) {
    static const uint8_t base[] = {18U, 32U, 46U};
    return high ? base[channel] - 4U : base[channel] + 2U;
}

static void tumoscope_format_frequency(
    const TumoScopeChannelStats* stats,
    char* output,
    size_t output_size) {
    if(stats->frequency_hz >= 1000000U) {
        snprintf(output, output_size, "%luM", (unsigned long)(stats->frequency_hz / 1000000U));
    } else if(stats->frequency_hz >= 1000U) {
        uint32_t whole = stats->frequency_hz / 1000U;
        uint32_t hundredths = (stats->frequency_hz % 1000U + 5U) / 10U;
        if(hundredths == 100U) {
            whole++;
            hundredths = 0U;
        }
        if(hundredths % 10U)
            snprintf(
                output, output_size, "%lu.%02luk", (unsigned long)whole, (unsigned long)hundredths);
        else if(hundredths)
            snprintf(
                output,
                output_size,
                "%lu.%luk",
                (unsigned long)whole,
                (unsigned long)(hundredths / 10U));
        else
            snprintf(output, output_size, "%luk", (unsigned long)whole);
    } else if(stats->frequency_hz > 0U) {
        snprintf(output, output_size, "%lu", (unsigned long)stats->frequency_hz);
    } else if(stats->transitions > 0U) {
        strlcpy(output, "edge", output_size);
    } else {
        strlcpy(output, "--", output_size);
    }
}

static void tumoscope_draw_waveform(Canvas* canvas, const TumoScopeApp* app) {
    canvas_set_font(canvas, FontSecondary);
    char header[48];
    size_t total_transitions = 0U;
    for(uint8_t channel = 0U; channel < TUMOSCOPE_CHANNEL_COUNT; channel++) {
        total_transitions += app->channel_stats[channel].transitions;
    }
    if(total_transitions == 0U) {
        strlcpy(header, "No transitions / static", sizeof(header));
    } else {
        char pc0[8];
        char pc1[8];
        char pc3[8];
        tumoscope_format_frequency(&app->channel_stats[0], pc0, sizeof(pc0));
        tumoscope_format_frequency(&app->channel_stats[1], pc1, sizeof(pc1));
        tumoscope_format_frequency(&app->channel_stats[2], pc3, sizeof(pc3));
        snprintf(header, sizeof(header), "0:%s 1:%s 3:%s Hz", pc0, pc1, pc3);
    }
    canvas_draw_str(canvas, 2, 9, header);

    static const char* const labels[] = {"0", "1", "3"};
    for(uint8_t channel = 0U; channel < TUMOSCOPE_CHANNEL_COUNT; channel++) {
        canvas_draw_str(canvas, 2, tumoscope_wave_y(channel, false), labels[channel]);
        for(uint8_t x = 0U; x < TUMOSCOPE_WAVE_WIDTH; x++) {
            const size_t first = app->wave_offset + (size_t)x * app->samples_per_pixel;
            if(first >= app->sample_count) break;
            const size_t last = MIN(first + app->samples_per_pixel, app->sample_count);
            bool low = false;
            bool high = false;
            for(size_t sample = first; sample < last; sample++) {
                if(tumoscope_sample_level(app->samples[sample], channel))
                    high = true;
                else
                    low = true;
            }
            const uint8_t screen_x = TUMOSCOPE_WAVE_LEFT + x;
            if(high && low) {
                canvas_draw_line(
                    canvas,
                    screen_x,
                    tumoscope_wave_y(channel, true),
                    screen_x,
                    tumoscope_wave_y(channel, false));
            } else {
                const uint8_t y = tumoscope_wave_y(channel, high);
                canvas_draw_dot(canvas, screen_x, y);
                if(x > 0U) canvas_draw_dot(canvas, screen_x - 1U, y);
            }
        }
    }

    if(app->trigger_sample >= app->wave_offset) {
        const size_t relative = app->trigger_sample - app->wave_offset;
        const size_t trigger_x = relative / app->samples_per_pixel;
        if(trigger_x < TUMOSCOPE_WAVE_WIDTH) {
            const uint8_t x = TUMOSCOPE_WAVE_LEFT + (uint8_t)trigger_x;
            canvas_draw_line(canvas, x, 11, x, 50);
            canvas_draw_triangle(canvas, x - 2U, 11, 5, 4, CanvasDirectionBottomToTop);
        }
    }

    canvas_draw_line(canvas, 0, 51, 127, 51);
    elements_button_left(canvas, "Pan");
    elements_button_center(canvas, "Decode");
    elements_button_right(canvas, "Pan");
}

static void tumoscope_format_decode_summary(
    const TumoScopeApp* app,
    char* first,
    size_t first_size,
    char* second,
    size_t second_size) {
    const TumoScopeDecoderProfile* profile = &tumoscope_decoders[app->decoder_index];
    switch(profile->decoder) {
    case TumoScopeDecoderUart:
        snprintf(
            first,
            first_size,
            "Frames:%lu Bytes:%u",
            (unsigned long)app->decode.frame_count,
            (unsigned int)app->decode.byte_count);
        snprintf(
            second, second_size, "Framing errors:%lu", (unsigned long)app->decode.error_count);
        break;
    case TumoScopeDecoderI2c:
        snprintf(
            first,
            first_size,
            "Starts:%lu Bytes:%u",
            (unsigned long)app->decode.frame_count,
            (unsigned int)app->decode.byte_count);
        snprintf(second, second_size, "NACK:%lu", (unsigned long)app->decode.error_count);
        break;
    case TumoScopeDecoderSpiMode0:
        snprintf(first, first_size, "Transfers:%lu", (unsigned long)app->decode.frame_count);
        snprintf(second, second_size, "Bytes:%u", (unsigned int)app->decode.byte_count);
        break;
    case TumoScopeDecoderOneWire:
        snprintf(first, first_size, "Reset pulses:%lu", (unsigned long)app->decode.event_count);
        snprintf(
            second,
            second_size,
            "Bytes:%u Partial:%lu",
            (unsigned int)app->decode.byte_count,
            (unsigned long)app->decode.error_count);
        break;
    case TumoScopeDecoderRaw:
    default:
        snprintf(first, first_size, "Raw digital capture");
        snprintf(second, second_size, "Use VCD for desktop view");
        break;
    }
}

static void tumoscope_draw_hex_row(
    Canvas* canvas,
    uint8_t y,
    const char* prefix,
    const uint8_t* bytes,
    size_t count) {
    char line[48];
    size_t offset = snprintf(line, sizeof(line), "%s", prefix);
    for(size_t index = 0U; index < MIN(count, 8U) && offset + 4U < sizeof(line); index++) {
        offset += snprintf(&line[offset], sizeof(line) - offset, "%02X ", bytes[index]);
    }
    canvas_draw_str(canvas, 2, y, line);
}

static void tumoscope_draw_decode(Canvas* canvas, const TumoScopeApp* app) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, tumoscope_decoders[app->decoder_index].label);
    canvas_set_font(canvas, FontSecondary);
    char first[40];
    char second[40];
    tumoscope_format_decode_summary(app, first, sizeof(first), second, sizeof(second));
    canvas_draw_str(canvas, 2, 22, first);
    canvas_draw_str(canvas, 2, 32, second);
    if(app->decode.byte_count > 0U) {
        tumoscope_draw_hex_row(canvas, 42, "D: ", app->decode.bytes, app->decode.byte_count);
        if(app->decode.decoder == TumoScopeDecoderSpiMode0) {
            tumoscope_draw_hex_row(
                canvas, 51, "I: ", app->decode.secondary, app->decode.byte_count);
        }
    } else {
        canvas_draw_str(canvas, 2, 43, app->exported ? "Saved to captures/" : app->status);
    }
    canvas_draw_line(canvas, 0, 53, 127, 53);
    elements_button_left(canvas, "Wave");
    elements_button_right(canvas, "Save");
}

static void tumoscope_draw(Canvas* canvas, void* context) {
    const TumoScopeApp* app = context;
    canvas_clear(canvas);
    switch(app->screen) {
    case TumoScopeScreenSetup:
        tumoscope_draw_setup(canvas, app);
        break;
    case TumoScopeScreenCapture:
        tumoscope_draw_capture(canvas, app);
        break;
    case TumoScopeScreenWaveform:
        tumoscope_draw_waveform(canvas, app);
        break;
    case TumoScopeScreenDecode:
        tumoscope_draw_decode(canvas, app);
        break;
    }
}

static void tumoscope_input(InputEvent* input, void* context) {
    TumoScopeApp* app = context;
    if(input->type != InputTypeShort && input->type != InputTypeRepeat) return;
    TumoScopeEvent event = {.input = *input};
    furi_message_queue_put(app->event_queue, &event, 0U);
}

static void tumoscope_change_index(uint8_t* value, uint8_t count, bool increment) {
    if(increment)
        *value = (uint8_t)((*value + 1U) % count);
    else
        *value = *value == 0U ? count - 1U : *value - 1U;
}

static void tumoscope_decode_capture(TumoScopeApp* app) {
    const TumoScopeDecoderProfile* profile = &tumoscope_decoders[app->decoder_index];
    switch(profile->decoder) {
    case TumoScopeDecoderUart: {
        const uint32_t baud = profile->demo == TumoScopeDemoUart ?
                                  tumoscope_demo_uart_baud(tumoscope_rates[app->rate_index]) :
                                  profile->baud;
        tumoscope_decode_uart(
            app->samples,
            app->sample_count,
            tumoscope_rates[app->rate_index],
            baud,
            0U,
            &app->decode);
        break;
    }
    case TumoScopeDecoderI2c:
        tumoscope_decode_i2c(app->samples, app->sample_count, 0U, 1U, &app->decode);
        break;
    case TumoScopeDecoderSpiMode0:
        tumoscope_decode_spi_mode0(app->samples, app->sample_count, 0U, 1U, 2U, &app->decode);
        break;
    case TumoScopeDecoderOneWire:
        tumoscope_decode_one_wire(
            app->samples, app->sample_count, tumoscope_rates[app->rate_index], 0U, &app->decode);
        break;
    case TumoScopeDecoderRaw:
    default:
        memset(&app->decode, 0, sizeof(app->decode));
        app->decode.decoder = TumoScopeDecoderRaw;
        break;
    }
}

static void tumoscope_prepare_result(TumoScopeApp* app, const char* status) {
    app->samples_per_pixel =
        MAX(1U, (app->sample_count + TUMOSCOPE_WAVE_WIDTH - 1U) / TUMOSCOPE_WAVE_WIDTH);
    app->wave_offset = 0U;
    for(uint8_t channel = 0U; channel < TUMOSCOPE_CHANNEL_COUNT; channel++) {
        tumoscope_analyze_channel(
            app->samples,
            app->sample_count,
            tumoscope_rates[app->rate_index],
            channel,
            &app->channel_stats[channel]);
    }
    tumoscope_decode_capture(app);
    app->screen = TumoScopeScreenWaveform;
    strlcpy(app->status, status, sizeof(app->status));
    notification_message(app->notifications, &sequence_success);
}

static bool tumoscope_run_demo(TumoScopeApp* app, const TumoScopeDecoderProfile* profile) {
    app->sample_count = tumoscope_depths[app->depth_index];
    if(!tumoscope_generate_demo(
           profile->demo, app->samples, app->sample_count, tumoscope_rates[app->rate_index])) {
        strlcpy(app->status, "Demo generation failed", sizeof(app->status));
        notification_message(app->notifications, &sequence_error);
        return false;
    }
    app->trigger_sample = app->sample_count / 4U;
    tumoscope_prepare_result(app, "Synthetic demo trace");
    return true;
}

static bool tumoscope_start_capture(TumoScopeApp* app) {
    const TumoScopeDecoderProfile* profile = &tumoscope_decoders[app->decoder_index];
    app->exported = false;
    app->status[0] = '\0';
    if(profile->demo != TumoScopeDemoNone) return tumoscope_run_demo(app, profile);

    TumoScopeCaptureConfig config = {
        .sample_rate = tumoscope_rates[app->rate_index],
        .sample_count = tumoscope_depths[app->depth_index],
        .trigger = (TumoScopeTrigger)app->trigger_index,
        .pretrigger_percent = 25U,
    };
    if(!tumoscope_capture_start(app->capture, &config)) {
        strlcpy(app->status, "Capture start failed", sizeof(app->status));
        notification_message(app->notifications, &sequence_error);
        return false;
    }
    app->screen = TumoScopeScreenCapture;
    return true;
}

static void tumoscope_cancel_capture(TumoScopeApp* app) {
    tumoscope_capture_stop(app->capture);
    app->screen = TumoScopeScreenSetup;
    strlcpy(app->status, "Capture cancelled", sizeof(app->status));
}

static void tumoscope_finish_capture(TumoScopeApp* app) {
    tumoscope_capture_stop(app->capture);
    app->sample_count = tumoscope_capture_count(app->capture);
    if(!app->sample_count ||
       !tumoscope_capture_copy(app->capture, app->samples, TUMOSCOPE_CAPTURE_MAX_SAMPLES)) {
        app->screen = TumoScopeScreenSetup;
        strlcpy(app->status, "Capture copy failed", sizeof(app->status));
        notification_message(app->notifications, &sequence_error);
        return;
    }
    app->trigger_sample = tumoscope_capture_trigger_index(app->capture);
    tumoscope_prepare_result(app, "Capture complete");
}

static bool tumoscope_write(File* file, const char* text) {
    const size_t length = strlen(text);
    return storage_file_write(file, text, length) == length;
}

static bool tumoscope_write_format(File* file, const char* format, ...) {
    char line[96];
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    return length >= 0 && (size_t)length < sizeof(line) &&
           storage_file_write(file, line, (size_t)length) == (size_t)length;
}

static bool tumoscope_export_vcd(TumoScopeApp* app, const char* path) {
    File* file = storage_file_alloc(app->storage);
    bool success = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(success) {
        success = tumoscope_write(
            file,
            "$date Tumoflip TumoScope $end\n"
            "$version TumoScope " TUMOSCOPE_APP_VERSION " $end\n"
            "$timescale 1 ns $end\n"
            "$scope module gpio $end\n"
            "$var wire 1 ! PC0 $end\n"
            "$var wire 1 \" PC1 $end\n"
            "$var wire 1 # PC3 $end\n"
            "$upscope $end\n"
            "$enddefinitions $end\n");
    }

    uint8_t previous = 0xFFU;
    char line[64];
    for(size_t index = 0U; success && index < app->sample_count; index++) {
        const uint8_t current = app->samples[index];
        if(current == previous) continue;
        const uint64_t timestamp =
            (uint64_t)index * 1000000000ULL / tumoscope_rates[app->rate_index];
        int length = snprintf(line, sizeof(line), "#%llu\n", (unsigned long long)timestamp);
        success = length > 0 && storage_file_write(file, line, (size_t)length) == (size_t)length;
        for(uint8_t channel = 0U; success && channel < TUMOSCOPE_CHANNEL_COUNT; channel++) {
            if(previous != 0xFFU && ((previous ^ current) & (1U << channel)) == 0U) continue;
            const char identifier = (char)('!' + channel);
            length = snprintf(
                line,
                sizeof(line),
                "%c%c\n",
                tumoscope_sample_level(current, channel) ? '1' : '0',
                identifier);
            success = storage_file_write(file, line, (size_t)length) == (size_t)length;
        }
        previous = current;
    }
    storage_file_close(file);
    storage_file_free(file);
    return success;
}

static bool tumoscope_export_report(TumoScopeApp* app, const char* path) {
    const TumoScopeDecoderProfile* profile = &tumoscope_decoders[app->decoder_index];
    File* file = storage_file_alloc(app->storage);
    bool success = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    char first[48];
    char second[48];
    tumoscope_format_decode_summary(app, first, sizeof(first), second, sizeof(second));
    if(success) success = tumoscope_write_format(file, "TumoScope %s\n", TUMOSCOPE_APP_VERSION);
    if(success)
        success = tumoscope_write_format(
            file, "Rate: %lu\n", (unsigned long)tumoscope_rates[app->rate_index]);
    if(success)
        success = tumoscope_write_format(file, "Samples: %u\n", (unsigned int)app->sample_count);
    if(success)
        success = tumoscope_write_format(
            file,
            "Trigger: %s @ %u\n",
            profile->demo == TumoScopeDemoNone ? tumoscope_trigger_labels[app->trigger_index] :
                                                 "Synthetic demo",
            (unsigned int)app->trigger_sample);
    if(success)
        success = tumoscope_write_format(
            file, "Decoder: %s\n", tumoscope_decoders[app->decoder_index].label);
    if(success)
        success = tumoscope_write_format(
            file, "Pins: %s\n", tumoscope_decoders[app->decoder_index].wiring);
    for(uint8_t channel = 0U; success && channel < TUMOSCOPE_CHANNEL_COUNT; channel++) {
        static const uint8_t pin_numbers[] = {0U, 1U, 3U};
        success = tumoscope_write_format(
            file,
            "PC%u: %lu Hz, %u transitions\n",
            pin_numbers[channel],
            (unsigned long)app->channel_stats[channel].frequency_hz,
            (unsigned int)app->channel_stats[channel].transitions);
    }
    if(success) success = tumoscope_write_format(file, "%s\n", first);
    if(success) success = tumoscope_write_format(file, "%s\nData:", second);
    char line[16];
    int length;
    for(size_t index = 0U; success && index < app->decode.byte_count; index++) {
        length = snprintf(line, sizeof(line), " %02X", app->decode.bytes[index]);
        success = storage_file_write(file, line, (size_t)length) == (size_t)length;
    }
    if(success) success = tumoscope_write(file, "\n");
    storage_file_close(file);
    storage_file_free(file);
    return success;
}

static void tumoscope_export(TumoScopeApp* app) {
    storage_common_mkdir(app->storage, TUMOSCOPE_DATA_DIR);
    storage_common_mkdir(app->storage, TUMOSCOPE_CAPTURE_DIR);
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    char stem[48];
    snprintf(
        stem,
        sizeof(stem),
        "scope_%04u%02u%02u_%02u%02u%02u",
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second);
    char vcd_path[128];
    char report_path[128];
    snprintf(vcd_path, sizeof(vcd_path), TUMOSCOPE_CAPTURE_DIR "/%s.vcd", stem);
    snprintf(report_path, sizeof(report_path), TUMOSCOPE_CAPTURE_DIR "/%s.txt", stem);
    const bool success = tumoscope_export_vcd(app, vcd_path) &&
                         tumoscope_export_report(app, report_path);
    if(success) {
        app->exported = true;
        strlcpy(app->export_path, vcd_path, sizeof(app->export_path));
        strlcpy(app->status, "Saved VCD + report", sizeof(app->status));
        notification_message(app->notifications, &sequence_success);
    } else {
        app->exported = false;
        strlcpy(app->status, "Export failed", sizeof(app->status));
        notification_message(app->notifications, &sequence_error);
    }
}

static void tumoscope_handle_setup(TumoScopeApp* app, const InputEvent* input) {
    if(input->key == InputKeyUp) {
        app->selected_row = app->selected_row == 0U ? 3U : app->selected_row - 1U;
    } else if(input->key == InputKeyDown) {
        app->selected_row = (uint8_t)((app->selected_row + 1U) % 4U);
    } else if(input->key == InputKeyLeft || input->key == InputKeyRight) {
        const bool increment = input->key == InputKeyRight;
        if(app->selected_row == 0U) {
            tumoscope_change_index(&app->rate_index, COUNT_OF(tumoscope_rates), increment);
        } else if(app->selected_row == 1U) {
            tumoscope_change_index(&app->depth_index, COUNT_OF(tumoscope_depths), increment);
        } else if(app->selected_row == 2U) {
            tumoscope_change_index(
                &app->trigger_index, COUNT_OF(tumoscope_trigger_labels), increment);
        } else {
            tumoscope_change_index(&app->decoder_index, COUNT_OF(tumoscope_decoders), increment);
        }
    } else if(input->key == InputKeyOk) {
        tumoscope_start_capture(app);
    } else if(input->key == InputKeyBack) {
        app->running = false;
    }
}

static void tumoscope_handle_waveform(TumoScopeApp* app, const InputEvent* input) {
    const size_t visible = app->samples_per_pixel * TUMOSCOPE_WAVE_WIDTH;
    if(input->key == InputKeyUp) {
        app->samples_per_pixel = MAX(1U, app->samples_per_pixel / 2U);
    } else if(input->key == InputKeyDown) {
        app->samples_per_pixel =
            MIN(MAX(1U, (app->sample_count + TUMOSCOPE_WAVE_WIDTH - 1U) / TUMOSCOPE_WAVE_WIDTH),
                app->samples_per_pixel * 2U);
    } else if(input->key == InputKeyLeft) {
        const size_t step = MAX(1U, visible / 4U);
        app->wave_offset = app->wave_offset > step ? app->wave_offset - step : 0U;
    } else if(input->key == InputKeyRight) {
        const size_t step = MAX(1U, visible / 4U);
        const size_t max_offset = app->sample_count > visible ? app->sample_count - visible : 0U;
        app->wave_offset = MIN(max_offset, app->wave_offset + step);
    } else if(input->key == InputKeyOk) {
        app->screen = TumoScopeScreenDecode;
    } else if(input->key == InputKeyBack) {
        app->screen = TumoScopeScreenSetup;
    }
}

static void tumoscope_process_event(TumoScopeApp* app, const TumoScopeEvent* event) {
    if(app->screen == TumoScopeScreenSetup) {
        tumoscope_handle_setup(app, &event->input);
    } else if(app->screen == TumoScopeScreenCapture) {
        if(event->input.key == InputKeyBack || event->input.key == InputKeyOk) {
            tumoscope_cancel_capture(app);
        }
    } else if(app->screen == TumoScopeScreenWaveform) {
        tumoscope_handle_waveform(app, &event->input);
    } else if(app->screen == TumoScopeScreenDecode) {
        if(event->input.key == InputKeyBack || event->input.key == InputKeyLeft) {
            app->screen = TumoScopeScreenWaveform;
        } else if(event->input.key == InputKeyRight || event->input.key == InputKeyOk) {
            tumoscope_export(app);
        }
    }
}

static TumoScopeApp* tumoscope_alloc(void) {
    TumoScopeApp* app = calloc(1U, sizeof(TumoScopeApp));
    if(!app) return NULL;
    app->samples = malloc(TUMOSCOPE_CAPTURE_MAX_SAMPLES);
    app->capture = tumoscope_capture_alloc();
    app->event_queue =
        furi_message_queue_alloc(TUMOSCOPE_EVENT_QUEUE_SIZE, sizeof(TumoScopeEvent));
    if(!app->samples || !app->capture || !app->event_queue) {
        if(app->event_queue) furi_message_queue_free(app->event_queue);
        tumoscope_capture_free(app->capture);
        free(app->samples);
        free(app);
        return NULL;
    }
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tumoscope_draw, app);
    view_port_input_callback_set(app->view_port, tumoscope_input, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->screen = TumoScopeScreenSetup;
    app->running = true;
    app->rate_index = 3U;
    app->depth_index = 2U;
    app->trigger_index = 0U;
    app->decoder_index = 1U;
    strlcpy(app->status, "Ready", sizeof(app->status));
    return app;
}

static void tumoscope_free(TumoScopeApp* app) {
    tumoscope_capture_free(app->capture);
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->event_queue);
    free(app->samples);
    free(app);
}

int32_t tumoscope_app(void* argument) {
    UNUSED(argument);
    TumoScopeApp* app = tumoscope_alloc();
    if(!app) return -1;
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);

    while(app->running) {
        TumoScopeEvent event;
        if(furi_message_queue_get(app->event_queue, &event, 50U) == FuriStatusOk) {
            tumoscope_process_event(app, &event);
        }
        if(app->screen == TumoScopeScreenCapture) {
            if(tumoscope_capture_has_error(app->capture)) {
                tumoscope_cancel_capture(app);
                strlcpy(app->status, "DMA capture error", sizeof(app->status));
                notification_message(app->notifications, &sequence_error);
            } else if(tumoscope_capture_is_complete(app->capture)) {
                tumoscope_finish_capture(app);
            }
        }
        view_port_update(app->view_port);
    }

    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    tumoscope_free(app);
    return 0;
}
