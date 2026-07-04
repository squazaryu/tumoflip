/*
 * Portions of this file are adapted from FlipIRFreq:
 * https://github.com/jsammarco/FlipIRFreq
 *
 * MIT License
 *
 * Copyright (c) 2026 Joe
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_infrared.h>
#include <furi_hal_resources.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#include <stdio.h>
#include <stdlib.h>

#define TUMO_IR_LAB_DEFAULT_CARRIER_FREQUENCY 38000U
#define TUMO_IR_LAB_DEFAULT_PULSE_FREQUENCY_TENTHS 380U
#define TUMO_IR_LAB_DEFAULT_DUTY_CYCLE 33U
#define TUMO_IR_LAB_DEFAULT_BURST_MS 250U
#define TUMO_IR_LAB_DEFAULT_REPEAT_COUNT 1U
#define TUMO_IR_LAB_TX_CHUNK_US 25000U
#define TUMO_IR_LAB_UI_TICK_HZ 8U
#define TUMO_IR_LAB_SETTINGS_PATH APP_DATA_PATH("tumo_ir_lab.settings")
#define TUMO_IR_LAB_SETTINGS_MAGIC 0x54
#define TUMO_IR_LAB_SETTINGS_VERSION 2U
#define TUMO_IR_LAB_PULSE_MIN_FREQUENCY_TENTHS 100U
#define TUMO_IR_LAB_PULSE_MAX_FREQUENCY_TENTHS 5000U
#define TUMO_IR_LAB_MIN_DUTY_CYCLE 1U
#define TUMO_IR_LAB_MAX_DUTY_CYCLE 99U
#define TUMO_IR_LAB_MIN_BURST_MS 1U
#define TUMO_IR_LAB_MAX_BURST_MS 5000U
#define TUMO_IR_LAB_MIN_REPEAT_COUNT 1U
#define TUMO_IR_LAB_MAX_REPEAT_COUNT 20U
#define TUMO_IR_LAB_VISIBLE_ROWS 4U
#define TUMO_IR_LAB_ROW_HEIGHT 9
#define TUMO_IR_LAB_REPEAT_GAP_MS 60U

typedef enum {
    TumoIrLabFieldFrequency,
    TumoIrLabFieldDutyCycle,
    TumoIrLabFieldBurst,
    TumoIrLabFieldSignal,
    TumoIrLabFieldMode,
    TumoIrLabFieldRepeat,
    TumoIrLabFieldOutput,
    TumoIrLabFieldSend,
    TumoIrLabFieldCount,
} TumoIrLabField;

typedef enum {
    TumoIrLabOutputAuto,
    TumoIrLabOutputInternal,
    TumoIrLabOutputModuleOne,
    TumoIrLabOutputCount,
} TumoIrLabOutput;

typedef enum {
    TumoIrLabModeBurst,
    TumoIrLabModeContinuous,
    TumoIrLabModeCount,
} TumoIrLabMode;

typedef enum {
    TumoIrLabSignalCarrier,
    TumoIrLabSignalPulse,
    TumoIrLabSignalCount,
} TumoIrLabSignal;

typedef enum {
    TumoIrLabEventTypeInput,
    TumoIrLabEventTypeTick,
    TumoIrLabEventTypeTxFinished,
    TumoIrLabEventTypeRepeatGap,
} TumoIrLabEventType;

typedef struct {
    TumoIrLabEventType type;
    InputEvent input;
} TumoIrLabEvent;

typedef struct {
    uint32_t carrier_frequency;
    uint32_t pulse_frequency_tenths;
    uint8_t duty_cycle;
    uint16_t burst_ms;
    uint8_t repeat_count;
    uint8_t output_mode;
    uint8_t tx_mode;
    uint8_t signal_mode;
} TumoIrLabSettings;

typedef struct {
    bool continuous;
    uint32_t remaining_us;
    uint32_t chunk_us;
} TumoIrLabTxContext;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    FuriTimer* ui_timer;
    FuriTimer* pulse_timer;
    FuriTimer* repeat_timer;

    uint32_t carrier_frequency;
    uint32_t pulse_frequency_tenths;
    uint8_t duty_cycle;
    uint16_t burst_ms;
    uint8_t repeat_count;
    uint8_t repeat_remaining;
    TumoIrLabField selected_field;
    uint8_t scroll_offset;
    TumoIrLabOutput output_mode;
    TumoIrLabMode tx_mode;
    TumoIrLabSignal signal_mode;

    bool running;
    bool transmitting;
    bool settings_dirty;
    uint32_t last_settings_change_tick;
    uint8_t tx_anim_phase;
    TumoIrLabTxContext tx_context;
    uint32_t pulse_on_ticks;
    uint32_t pulse_off_ticks;
    uint32_t pulse_remaining_ticks;
    bool pulse_level;
    const GpioPin* pulse_pin;
    char status[16];
} TumoIrLabApp;

static void tumo_ir_lab_ensure_selection_visible(TumoIrLabApp* app);

static uint32_t tumo_ir_lab_clamp_i32_to_u32(int32_t value, uint32_t min, uint32_t max) {
    if(value < (int32_t)min) return min;
    if((uint32_t)value > max) return max;
    return (uint32_t)value;
}

static uint8_t tumo_ir_lab_clamp_i32_to_u8(int32_t value, uint8_t min, uint8_t max) {
    if(value < min) return min;
    if(value > max) return max;
    return (uint8_t)value;
}

static const char* tumo_ir_lab_output_short_label(FuriHalInfraredTxPin output) {
    switch(output) {
    case FuriHalInfraredTxPinInternal:
        return "INT";
    case FuriHalInfraredTxPinExtPA7:
        return "MOD1";
    default:
        return "?";
    }
}

static const char* tumo_ir_lab_output_mode_label(TumoIrLabOutput mode) {
    switch(mode) {
    case TumoIrLabOutputAuto:
        return "AUTO";
    case TumoIrLabOutputInternal:
        return "INT";
    case TumoIrLabOutputModuleOne:
        return "MOD1";
    default:
        return "?";
    }
}

static const char* tumo_ir_lab_mode_label(TumoIrLabMode mode) {
    switch(mode) {
    case TumoIrLabModeBurst:
        return "BURST";
    case TumoIrLabModeContinuous:
        return "CONT";
    default:
        return "?";
    }
}

static const char* tumo_ir_lab_signal_label(TumoIrLabSignal mode) {
    switch(mode) {
    case TumoIrLabSignalCarrier:
        return "CARR";
    case TumoIrLabSignalPulse:
        return "PULSE";
    default:
        return "?";
    }
}

static FuriHalInfraredTxPin tumo_ir_lab_resolve_output(const TumoIrLabApp* app) {
    if(app->output_mode == TumoIrLabOutputInternal) {
        return FuriHalInfraredTxPinInternal;
    } else if(app->output_mode == TumoIrLabOutputModuleOne) {
        return FuriHalInfraredTxPinExtPA7;
    }

    return furi_hal_infrared_detect_tx_output();
}

static const GpioPin* tumo_ir_lab_resolve_output_gpio(FuriHalInfraredTxPin output) {
    switch(output) {
    case FuriHalInfraredTxPinInternal:
        return &gpio_infrared_tx;
    case FuriHalInfraredTxPinExtPA7:
        return &gpio_ext_pa7;
    default:
        return NULL;
    }
}

static uint32_t tumo_ir_lab_frequency_min(const TumoIrLabApp* app) {
    return (app->signal_mode == TumoIrLabSignalPulse) ? TUMO_IR_LAB_PULSE_MIN_FREQUENCY_TENTHS :
                                                        INFRARED_MIN_FREQUENCY;
}

static uint32_t tumo_ir_lab_frequency_max(const TumoIrLabApp* app) {
    return (app->signal_mode == TumoIrLabSignalPulse) ? TUMO_IR_LAB_PULSE_MAX_FREQUENCY_TENTHS :
                                                        INFRARED_MAX_FREQUENCY;
}

static uint32_t tumo_ir_lab_frequency_step(const TumoIrLabApp* app, bool coarse) {
    if(app->signal_mode == TumoIrLabSignalPulse) {
        return coarse ? 10U : 1U;
    }

    return coarse ? 1000U : 100U;
}

static uint16_t tumo_ir_lab_burst_step(bool coarse) {
    return coarse ? 100U : 10U;
}

static void tumo_ir_lab_set_status(TumoIrLabApp* app, const char* text) {
    snprintf(app->status, sizeof(app->status), "%s", text);
}

static void tumo_ir_lab_mark_settings_dirty(TumoIrLabApp* app) {
    app->settings_dirty = true;
    app->last_settings_change_tick = furi_get_tick();
}

static void tumo_ir_lab_load_defaults(TumoIrLabApp* app) {
    app->carrier_frequency = TUMO_IR_LAB_DEFAULT_CARRIER_FREQUENCY;
    app->pulse_frequency_tenths = TUMO_IR_LAB_DEFAULT_PULSE_FREQUENCY_TENTHS;
    app->duty_cycle = TUMO_IR_LAB_DEFAULT_DUTY_CYCLE;
    app->burst_ms = TUMO_IR_LAB_DEFAULT_BURST_MS;
    app->repeat_count = TUMO_IR_LAB_DEFAULT_REPEAT_COUNT;
    app->output_mode = TumoIrLabOutputAuto;
    app->tx_mode = TumoIrLabModeBurst;
    app->signal_mode = TumoIrLabSignalCarrier;
}

static void tumo_ir_lab_save_settings(TumoIrLabApp* app) {
    TumoIrLabSettings settings = {
        .carrier_frequency = app->carrier_frequency,
        .pulse_frequency_tenths = app->pulse_frequency_tenths,
        .duty_cycle = app->duty_cycle,
        .burst_ms = app->burst_ms,
        .repeat_count = app->repeat_count,
        .output_mode = app->output_mode,
        .tx_mode = app->tx_mode,
        .signal_mode = app->signal_mode,
    };

    if(saved_struct_save(
           TUMO_IR_LAB_SETTINGS_PATH,
           &settings,
           sizeof(settings),
           TUMO_IR_LAB_SETTINGS_MAGIC,
           TUMO_IR_LAB_SETTINGS_VERSION)) {
        app->settings_dirty = false;
    }
}

static void tumo_ir_lab_load_settings(TumoIrLabApp* app) {
    TumoIrLabSettings settings = {0};
    tumo_ir_lab_load_defaults(app);

    if(saved_struct_load(
           TUMO_IR_LAB_SETTINGS_PATH,
           &settings,
           sizeof(settings),
           TUMO_IR_LAB_SETTINGS_MAGIC,
           TUMO_IR_LAB_SETTINGS_VERSION)) {
        app->carrier_frequency = tumo_ir_lab_clamp_i32_to_u32(
            settings.carrier_frequency, INFRARED_MIN_FREQUENCY, INFRARED_MAX_FREQUENCY);
        app->pulse_frequency_tenths = tumo_ir_lab_clamp_i32_to_u32(
            settings.pulse_frequency_tenths,
            TUMO_IR_LAB_PULSE_MIN_FREQUENCY_TENTHS,
            TUMO_IR_LAB_PULSE_MAX_FREQUENCY_TENTHS);
        app->duty_cycle = tumo_ir_lab_clamp_i32_to_u8(
            settings.duty_cycle, TUMO_IR_LAB_MIN_DUTY_CYCLE, TUMO_IR_LAB_MAX_DUTY_CYCLE);
        app->burst_ms = tumo_ir_lab_clamp_i32_to_u32(
            settings.burst_ms, TUMO_IR_LAB_MIN_BURST_MS, TUMO_IR_LAB_MAX_BURST_MS);
        app->repeat_count = tumo_ir_lab_clamp_i32_to_u8(
            settings.repeat_count, TUMO_IR_LAB_MIN_REPEAT_COUNT, TUMO_IR_LAB_MAX_REPEAT_COUNT);
        app->output_mode = (settings.output_mode < TumoIrLabOutputCount) ?
                               (TumoIrLabOutput)settings.output_mode :
                               TumoIrLabOutputAuto;
        app->tx_mode = (settings.tx_mode < TumoIrLabModeCount) ?
                           (TumoIrLabMode)settings.tx_mode :
                           TumoIrLabModeBurst;
        app->signal_mode = (settings.signal_mode < TumoIrLabSignalCount) ?
                               (TumoIrLabSignal)settings.signal_mode :
                               TumoIrLabSignalCarrier;
    }
}

static uint32_t tumo_ir_lab_get_active_frequency(const TumoIrLabApp* app) {
    return (app->signal_mode == TumoIrLabSignalPulse) ? app->pulse_frequency_tenths :
                                                        app->carrier_frequency;
}

static void tumo_ir_lab_set_active_frequency(TumoIrLabApp* app, uint32_t value) {
    if(app->signal_mode == TumoIrLabSignalPulse) {
        app->pulse_frequency_tenths = value;
    } else {
        app->carrier_frequency = value;
    }
}

static FuriHalInfraredTxGetDataState
    tumo_ir_lab_tx_data_callback(void* context, uint32_t* duration, bool* level) {
    TumoIrLabTxContext* tx_context = context;
    *level = true;

    if(tx_context->continuous) {
        *duration = tx_context->chunk_us;
        return FuriHalInfraredTxGetDataStateDone;
    }

    if(tx_context->remaining_us > tx_context->chunk_us) {
        tx_context->remaining_us -= tx_context->chunk_us;
        *duration = tx_context->chunk_us;
        return FuriHalInfraredTxGetDataStateDone;
    }

    *duration = tx_context->remaining_us;
    tx_context->remaining_us = 0;
    return FuriHalInfraredTxGetDataStateLastDone;
}

static void tumo_ir_lab_tx_finished_callback(void* context) {
    TumoIrLabApp* app = context;
    TumoIrLabEvent event = {.type = TumoIrLabEventTypeTxFinished};
    furi_message_queue_put(app->event_queue, &event, 0);
}

static void tumo_ir_lab_repeat_gap_callback(void* context) {
    TumoIrLabApp* app = context;
    TumoIrLabEvent event = {.type = TumoIrLabEventTypeRepeatGap};
    furi_message_queue_put(app->event_queue, &event, 0);
}

static void tumo_ir_lab_pulse_release_pin(TumoIrLabApp* app) {
    if(app->pulse_pin) {
        furi_hal_gpio_write(app->pulse_pin, false);
        furi_hal_gpio_init(app->pulse_pin, GpioModeAnalog, GpioPullDown, GpioSpeedLow);
        app->pulse_pin = NULL;
    }
}

static uint32_t tumo_ir_lab_pulse_period_ticks(uint32_t frequency_tenths) {
    const uint32_t tick_hz = furi_kernel_get_tick_frequency();
    uint32_t ticks = ((tick_hz * 10U) + (frequency_tenths / 2U)) / frequency_tenths;
    return (ticks < 2U) ? 2U : ticks;
}

static void tumo_ir_lab_pulse_timer_callback(void* context) {
    TumoIrLabApp* app = context;

    if(!app->transmitting || (app->signal_mode != TumoIrLabSignalPulse)) {
        return;
    }

    if((app->tx_mode == TumoIrLabModeBurst) && (app->pulse_remaining_ticks == 0U)) {
        tumo_ir_lab_tx_finished_callback(app);
        return;
    }

    app->pulse_level = !app->pulse_level;
    furi_hal_gpio_write(app->pulse_pin, app->pulse_level);

    uint32_t next_ticks = app->pulse_level ? app->pulse_on_ticks : app->pulse_off_ticks;
    if(app->tx_mode == TumoIrLabModeBurst) {
        if(app->pulse_remaining_ticks <= next_ticks) {
            next_ticks = app->pulse_remaining_ticks;
            app->pulse_remaining_ticks = 0U;
        } else {
            app->pulse_remaining_ticks -= next_ticks;
        }
    }

    if(next_ticks == 0U) {
        tumo_ir_lab_tx_finished_callback(app);
        return;
    }

    furi_timer_start(app->pulse_timer, next_ticks);
}

static void tumo_ir_lab_stop_current_transmit(TumoIrLabApp* app) {
    if(!app->transmitting) {
        return;
    }

    if(app->signal_mode == TumoIrLabSignalPulse) {
        furi_timer_stop(app->pulse_timer);
        tumo_ir_lab_pulse_release_pin(app);
    } else {
        furi_hal_infrared_async_tx_stop();
        furi_hal_infrared_async_tx_set_signal_sent_isr_callback(NULL, NULL);
        furi_hal_infrared_async_tx_set_data_isr_callback(NULL, NULL);
    }

    app->transmitting = false;
    app->tx_anim_phase = 0;
    app->pulse_level = false;
    app->pulse_remaining_ticks = 0;
}

static void tumo_ir_lab_stop_transmit(TumoIrLabApp* app, bool interrupted) {
    furi_timer_stop(app->repeat_timer);
    tumo_ir_lab_stop_current_transmit(app);
    app->repeat_remaining = 0;
    tumo_ir_lab_set_status(app, interrupted ? "stopped" : "DONE");
}

static bool tumo_ir_lab_start_current_transmit(TumoIrLabApp* app) {
    if(furi_hal_infrared_is_busy()) {
        tumo_ir_lab_set_status(app, "BUSY");
        return false;
    }

    const FuriHalInfraredTxPin output = tumo_ir_lab_resolve_output(app);
    if(app->signal_mode == TumoIrLabSignalPulse) {
        const GpioPin* gpio = tumo_ir_lab_resolve_output_gpio(output);
        if(!gpio) {
            tumo_ir_lab_set_status(app, "NO OUT");
            return false;
        }

        const uint32_t period_ticks =
            tumo_ir_lab_pulse_period_ticks(app->pulse_frequency_tenths);
        uint32_t on_ticks = (period_ticks * app->duty_cycle) / 100U;
        if(on_ticks == 0U) on_ticks = 1U;
        if(on_ticks >= period_ticks) on_ticks = period_ticks - 1U;

        app->pulse_pin = gpio;
        app->pulse_on_ticks = on_ticks;
        app->pulse_off_ticks = period_ticks - on_ticks;
        app->pulse_remaining_ticks =
            (app->tx_mode == TumoIrLabModeBurst) ? furi_ms_to_ticks(app->burst_ms) : 0U;
        app->pulse_level = true;

        furi_hal_gpio_write(gpio, false);
        furi_hal_gpio_init(gpio, GpioModeOutputPushPull, GpioPullDown, GpioSpeedHigh);
        furi_hal_gpio_write(gpio, true);

        if(app->tx_mode == TumoIrLabModeBurst) {
            if(app->pulse_remaining_ticks <= app->pulse_on_ticks) {
                app->pulse_on_ticks = app->pulse_remaining_ticks;
                app->pulse_remaining_ticks = 0U;
            } else {
                app->pulse_remaining_ticks -= app->pulse_on_ticks;
            }
        }

        furi_timer_start(app->pulse_timer, app->pulse_on_ticks);
    } else {
        app->tx_context = (TumoIrLabTxContext){
            .continuous = (app->tx_mode == TumoIrLabModeContinuous),
            .remaining_us = (uint32_t)app->burst_ms * 1000U,
            .chunk_us = TUMO_IR_LAB_TX_CHUNK_US,
        };

        furi_hal_infrared_set_tx_output(output);
        furi_hal_infrared_async_tx_set_data_isr_callback(
            tumo_ir_lab_tx_data_callback, &app->tx_context);
        furi_hal_infrared_async_tx_set_signal_sent_isr_callback(
            (app->tx_mode == TumoIrLabModeBurst) ? tumo_ir_lab_tx_finished_callback : NULL,
            app);
        furi_hal_infrared_async_tx_start(
            app->carrier_frequency, (float)app->duty_cycle / 100.0f);
    }

    app->transmitting = true;
    app->tx_anim_phase = 0;
    app->selected_field = TumoIrLabFieldSend;
    tumo_ir_lab_ensure_selection_visible(app);
    tumo_ir_lab_set_status(app, "LIVE");
    return true;
}

static void tumo_ir_lab_start_transmit(TumoIrLabApp* app) {
    app->repeat_remaining =
        (app->tx_mode == TumoIrLabModeBurst) ? app->repeat_count : TUMO_IR_LAB_MIN_REPEAT_COUNT;

    if(!tumo_ir_lab_start_current_transmit(app)) {
        app->repeat_remaining = 0;
    }
}

static void tumo_ir_lab_handle_tx_finished(TumoIrLabApp* app) {
    if(!app->transmitting) {
        return;
    }

    const uint8_t remaining = app->repeat_remaining;
    tumo_ir_lab_stop_current_transmit(app);

    if((app->tx_mode == TumoIrLabModeBurst) && (remaining > 1U)) {
        app->repeat_remaining = remaining - 1U;
        tumo_ir_lab_set_status(app, "NEXT");
        furi_timer_start(app->repeat_timer, furi_ms_to_ticks(TUMO_IR_LAB_REPEAT_GAP_MS));
    } else {
        app->repeat_remaining = 0;
        tumo_ir_lab_set_status(app, "DONE");
    }
}

static void tumo_ir_lab_handle_repeat_gap(TumoIrLabApp* app) {
    if(!app->transmitting && (app->repeat_remaining > 0U)) {
        if(!tumo_ir_lab_start_current_transmit(app)) {
            app->repeat_remaining = 0;
        }
    }
}

static void tumo_ir_lab_adjust_field(TumoIrLabApp* app, bool increase, bool coarse) {
    const int32_t direction = increase ? 1 : -1;

    switch(app->selected_field) {
    case TumoIrLabFieldFrequency: {
        int32_t next = (int32_t)tumo_ir_lab_get_active_frequency(app) +
                       (int32_t)tumo_ir_lab_frequency_step(app, coarse) * direction;
        tumo_ir_lab_set_active_frequency(
            app,
            tumo_ir_lab_clamp_i32_to_u32(
                next, tumo_ir_lab_frequency_min(app), tumo_ir_lab_frequency_max(app)));
        break;
    }
    case TumoIrLabFieldDutyCycle: {
        int32_t next = (int32_t)app->duty_cycle + (coarse ? 5 : 1) * direction;
        app->duty_cycle =
            tumo_ir_lab_clamp_i32_to_u8(next, TUMO_IR_LAB_MIN_DUTY_CYCLE, TUMO_IR_LAB_MAX_DUTY_CYCLE);
        break;
    }
    case TumoIrLabFieldBurst: {
        int32_t next = (int32_t)app->burst_ms + (int32_t)tumo_ir_lab_burst_step(coarse) * direction;
        app->burst_ms =
            tumo_ir_lab_clamp_i32_to_u32(next, TUMO_IR_LAB_MIN_BURST_MS, TUMO_IR_LAB_MAX_BURST_MS);
        break;
    }
    case TumoIrLabFieldSignal: {
        int32_t next = (int32_t)app->signal_mode + direction;
        if(next < 0) {
            next = TumoIrLabSignalCount - 1;
        } else if(next >= TumoIrLabSignalCount) {
            next = 0;
        }
        app->signal_mode = next;
        break;
    }
    case TumoIrLabFieldMode: {
        int32_t next = (int32_t)app->tx_mode + direction;
        if(next < 0) {
            next = TumoIrLabModeCount - 1;
        } else if(next >= TumoIrLabModeCount) {
            next = 0;
        }
        app->tx_mode = next;
        break;
    }
    case TumoIrLabFieldRepeat: {
        int32_t next = (int32_t)app->repeat_count + direction;
        app->repeat_count = tumo_ir_lab_clamp_i32_to_u8(
            next, TUMO_IR_LAB_MIN_REPEAT_COUNT, TUMO_IR_LAB_MAX_REPEAT_COUNT);
        break;
    }
    case TumoIrLabFieldOutput: {
        int32_t next = (int32_t)app->output_mode + direction;
        if(next < 0) {
            next = TumoIrLabOutputCount - 1;
        } else if(next >= TumoIrLabOutputCount) {
            next = 0;
        }
        app->output_mode = next;
        break;
    }
    default:
        break;
    }
}

static const char* tumo_ir_lab_tx_indicator(uint8_t phase) {
    static const char* frames[] = {"TX", "TX.", "TX..", "TX..."};
    return frames[phase % COUNT_OF(frames)];
}

static void tumo_ir_lab_ensure_selection_visible(TumoIrLabApp* app) {
    if(app->selected_field < app->scroll_offset) {
        app->scroll_offset = app->selected_field;
    } else if(app->selected_field >= (app->scroll_offset + TUMO_IR_LAB_VISIBLE_ROWS)) {
        app->scroll_offset = app->selected_field - TUMO_IR_LAB_VISIBLE_ROWS + 1U;
    }
}

static const char* tumo_ir_lab_field_label(TumoIrLabField field) {
    switch(field) {
    case TumoIrLabFieldFrequency:
        return "Frequency";
    case TumoIrLabFieldDutyCycle:
        return "Duty Cycle";
    case TumoIrLabFieldBurst:
        return "Burst Time";
    case TumoIrLabFieldSignal:
        return "Signal Type";
    case TumoIrLabFieldMode:
        return "Tx Mode";
    case TumoIrLabFieldRepeat:
        return "Repeat";
    case TumoIrLabFieldOutput:
        return "IR Output";
    case TumoIrLabFieldSend:
        return "Transmit";
    default:
        return "?";
    }
}

static void tumo_ir_lab_field_value(
    const TumoIrLabApp* app,
    TumoIrLabField field,
    char* value,
    size_t size) {
    switch(field) {
    case TumoIrLabFieldFrequency:
        if(app->signal_mode == TumoIrLabSignalPulse) {
            snprintf(
                value,
                size,
                "%lu.%lu Hz",
                (unsigned long)(app->pulse_frequency_tenths / 10U),
                (unsigned long)(app->pulse_frequency_tenths % 10U));
        } else {
            snprintf(
                value,
                size,
                "%lu.%lu kHz",
                (unsigned long)(app->carrier_frequency / 1000U),
                (unsigned long)((app->carrier_frequency % 1000U) / 100U));
        }
        break;
    case TumoIrLabFieldDutyCycle:
        snprintf(value, size, "%u%%", app->duty_cycle);
        break;
    case TumoIrLabFieldBurst:
        snprintf(value, size, "%u ms", (unsigned int)app->burst_ms);
        break;
    case TumoIrLabFieldSignal:
        snprintf(value, size, "%s", tumo_ir_lab_signal_label(app->signal_mode));
        break;
    case TumoIrLabFieldMode:
        snprintf(value, size, "%s", tumo_ir_lab_mode_label(app->tx_mode));
        break;
    case TumoIrLabFieldRepeat:
        snprintf(value, size, "%ux", app->repeat_count);
        break;
    case TumoIrLabFieldOutput:
        if(app->output_mode == TumoIrLabOutputAuto) {
            snprintf(
                value,
                size,
                "AUTO>%s",
                tumo_ir_lab_output_short_label(tumo_ir_lab_resolve_output(app)));
        } else {
            snprintf(value, size, "%s", tumo_ir_lab_output_mode_label(app->output_mode));
        }
        break;
    case TumoIrLabFieldSend:
        snprintf(
            value,
            size,
            "%s",
            app->transmitting ? "STOP" : (app->tx_mode == TumoIrLabModeContinuous ? "START" : "SEND"));
        break;
    default:
        value[0] = '\0';
        break;
    }
}

static void tumo_ir_lab_draw_row(
    Canvas* canvas,
    int32_t y,
    bool selected,
    const char* label,
    const char* value) {
    if(selected) {
        canvas_draw_box(canvas, 0, y - 7, 128, 9);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
    }

    canvas_draw_str(canvas, 3, y, label);
    canvas_draw_str_aligned(canvas, 125, y, AlignRight, AlignBottom, value);

    if(selected) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void tumo_ir_lab_draw_callback(Canvas* canvas, void* context) {
    TumoIrLabApp* app = context;
    char value[32];

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "Tumo IR Lab");
    if(app->transmitting) {
        canvas_draw_str_aligned(
            canvas, 125, 9, AlignRight, AlignBottom, tumo_ir_lab_tx_indicator(app->tx_anim_phase));
    } else if(app->status[0] != '\0') {
        canvas_draw_str_aligned(canvas, 125, 9, AlignRight, AlignBottom, app->status);
    }

    canvas_set_font(canvas, FontSecondary);

    for(uint8_t row = 0; row < TUMO_IR_LAB_VISIBLE_ROWS; row++) {
        const uint8_t field_index = app->scroll_offset + row;
        if(field_index >= TumoIrLabFieldCount) break;

        const TumoIrLabField field = (TumoIrLabField)field_index;
        tumo_ir_lab_field_value(app, field, value, sizeof(value));
        tumo_ir_lab_draw_row(
            canvas,
            18 + (row * TUMO_IR_LAB_ROW_HEIGHT),
            app->selected_field == field,
            tumo_ir_lab_field_label(field),
            value);
    }

    if(app->scroll_offset > 0) {
        canvas_draw_str(canvas, 120, 16, "^");
    }
    if((app->scroll_offset + TUMO_IR_LAB_VISIBLE_ROWS) < TumoIrLabFieldCount) {
        canvas_draw_str(canvas, 120, 53, "v");
    }

    canvas_draw_line(canvas, 2, 54, 126, 54);
    canvas_draw_str(canvas, 2, 62, "Module One IR Lab");
}

static void tumo_ir_lab_input_callback(InputEvent* input_event, void* context) {
    TumoIrLabApp* app = context;
    TumoIrLabEvent event = {.type = TumoIrLabEventTypeInput, .input = *input_event};
    furi_message_queue_put(app->event_queue, &event, 0);
}

static void tumo_ir_lab_handle_input(TumoIrLabApp* app, const InputEvent* input) {
    const bool pressed = (input->type == InputTypeShort) || (input->type == InputTypeRepeat);
    const bool coarse = input->type == InputTypeRepeat;

    if(input->key == InputKeyBack && input->type == InputTypeShort) {
        if(app->transmitting || (app->repeat_remaining > 0U)) {
            tumo_ir_lab_stop_transmit(app, true);
            return;
        }

        app->running = false;
        return;
    }

    if(app->transmitting || (app->repeat_remaining > 0U)) {
        if((input->key == InputKeyOk) && (input->type == InputTypeShort)) {
            tumo_ir_lab_stop_transmit(app, true);
        }
        return;
    }

    if(!pressed && input->key != InputKeyOk) {
        return;
    }

    switch(input->key) {
    case InputKeyUp:
        if(pressed) {
            if(app->selected_field == 0) {
                app->selected_field = TumoIrLabFieldCount - 1;
            } else {
                app->selected_field--;
            }
            tumo_ir_lab_ensure_selection_visible(app);
        }
        break;
    case InputKeyDown:
        if(pressed) {
            app->selected_field = (app->selected_field + 1) % TumoIrLabFieldCount;
            tumo_ir_lab_ensure_selection_visible(app);
        }
        break;
    case InputKeyLeft:
        if(pressed) {
            tumo_ir_lab_adjust_field(app, false, coarse);
            if(app->selected_field != TumoIrLabFieldSend) {
                tumo_ir_lab_mark_settings_dirty(app);
            }
        }
        break;
    case InputKeyRight:
        if(pressed) {
            tumo_ir_lab_adjust_field(app, true, coarse);
            if(app->selected_field != TumoIrLabFieldSend) {
                tumo_ir_lab_mark_settings_dirty(app);
            }
        }
        break;
    case InputKeyOk:
        if((input->type == InputTypeShort) && (app->selected_field == TumoIrLabFieldSend)) {
            tumo_ir_lab_start_transmit(app);
        }
        break;
    default:
        break;
    }
}

static void tumo_ir_lab_tick_callback(void* context) {
    TumoIrLabApp* app = context;
    TumoIrLabEvent event = {.type = TumoIrLabEventTypeTick};
    furi_message_queue_put(app->event_queue, &event, 0);
}

static void tumo_ir_lab_handle_tick(TumoIrLabApp* app) {
    if(app->transmitting) {
        app->tx_anim_phase = (app->tx_anim_phase + 1) & 0x03;
    }

    if(app->settings_dirty &&
       ((furi_get_tick() - app->last_settings_change_tick) >= (furi_kernel_get_tick_frequency() / 2))) {
        tumo_ir_lab_save_settings(app);
    }
}

static TumoIrLabApp* tumo_ir_lab_app_alloc(void) {
    TumoIrLabApp* app = malloc(sizeof(TumoIrLabApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    app->event_queue = furi_message_queue_alloc(8, sizeof(TumoIrLabEvent));
    app->ui_timer = furi_timer_alloc(tumo_ir_lab_tick_callback, FuriTimerTypePeriodic, app);
    app->pulse_timer = furi_timer_alloc(tumo_ir_lab_pulse_timer_callback, FuriTimerTypeOnce, app);
    app->repeat_timer = furi_timer_alloc(tumo_ir_lab_repeat_gap_callback, FuriTimerTypeOnce, app);

    tumo_ir_lab_load_settings(app);
    app->selected_field = TumoIrLabFieldFrequency;
    app->scroll_offset = 0;
    app->running = true;
    app->transmitting = false;
    app->settings_dirty = false;
    app->last_settings_change_tick = 0;
    app->tx_anim_phase = 0;
    app->repeat_remaining = 0;
    app->pulse_on_ticks = 0;
    app->pulse_off_ticks = 0;
    app->pulse_remaining_ticks = 0;
    app->pulse_level = false;
    app->pulse_pin = NULL;
    tumo_ir_lab_set_status(app, "");

    view_port_draw_callback_set(app->view_port, tumo_ir_lab_draw_callback, app);
    view_port_input_callback_set(app->view_port, tumo_ir_lab_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    furi_timer_start(app->ui_timer, furi_kernel_get_tick_frequency() / TUMO_IR_LAB_UI_TICK_HZ);

    return app;
}

static void tumo_ir_lab_app_free(TumoIrLabApp* app) {
    if(!app) return;

    tumo_ir_lab_stop_transmit(app, true);
    if(app->settings_dirty) {
        tumo_ir_lab_save_settings(app);
    }
    furi_timer_stop(app->repeat_timer);
    furi_timer_free(app->repeat_timer);
    furi_timer_stop(app->pulse_timer);
    furi_timer_free(app->pulse_timer);
    furi_timer_stop(app->ui_timer);
    furi_timer_free(app->ui_timer);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumo_ir_lab_app(void* p) {
    UNUSED(p);

    TumoIrLabApp* app = tumo_ir_lab_app_alloc();
    TumoIrLabEvent event;

    view_port_update(app->view_port);

    while(app->running) {
        if(furi_message_queue_get(app->event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            switch(event.type) {
            case TumoIrLabEventTypeInput:
                tumo_ir_lab_handle_input(app, &event.input);
                break;
            case TumoIrLabEventTypeTick:
                tumo_ir_lab_handle_tick(app);
                break;
            case TumoIrLabEventTypeTxFinished:
                tumo_ir_lab_handle_tx_finished(app);
                break;
            case TumoIrLabEventTypeRepeatGap:
                tumo_ir_lab_handle_repeat_gap(app);
                break;
            default:
                break;
            }
            view_port_update(app->view_port);
        }
    }

    tumo_ir_lab_app_free(app);
    return 0;
}
