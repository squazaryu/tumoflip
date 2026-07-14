#include "subghz_frequency_analyzer.h"

#include <furi.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <gui/elements.h>
#include "../helpers/subghz_frequency_analyzer_worker.h"

#include <assets_icons.h>
#include <float_tools.h>
#include <string.h>

#define TAG "frequency_analyzer"

#define RSSI_MIN     (-97.0f)
#define RSSI_MAX     (-60.0f)
#define RSSI_SCALE   2.3f
#define TRIGGER_STEP 1
#define MAX_HISTORY  4
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

typedef enum {
    SubGhzFrequencyAnalyzerStatusIDLE,
} SubGhzFrequencyAnalyzerStatus;

typedef enum {
    SubGhzFrequencyAnalyzerModeFrequency,
    SubGhzFrequencyAnalyzerModePreset,
} SubGhzFrequencyAnalyzerMode;

struct SubGhzFrequencyAnalyzer {
    View* view;
    SubGhzFrequencyAnalyzerWorker* worker;
    SubGhzFrequencyAnalyzerCallback callback;
    void* context;
    SubGhzTxRx* txrx;
    bool locked;
    SubGHzFrequencyAnalyzerFeedbackLevel
        feedback_level; // 0 - no feedback, 1 - vibro only, 2 - vibro and sound
    float rssi_last;
    uint8_t selected_index;
    uint8_t max_index;
    bool show_frame;
    SubGhzFrequencyAnalyzerObservation last_observation;
    SubGhzFrequencyAnalyzerNotebookTag notebook_tag;
    SubGhzFrequencyAnalyzerMode mode;
    uint32_t preset_frequency;
    size_t preset_selected_rank;
    size_t preset_result_count;
    bool preset_scan_complete;
};

typedef struct {
    uint32_t frequency;
    uint32_t frequency_to_save;
    float rssi;
    uint32_t history_frequency[MAX_HISTORY];
    uint8_t history_frequency_rx_count[MAX_HISTORY];
    bool signal;
    float rssi_last;
    float trigger;
    SubGHzFrequencyAnalyzerFeedbackLevel feedback_level;
    uint8_t selected_index;
    uint8_t max_index;
    bool show_frame;
    bool is_ext_radio;
    SubGhzFrequencyAnalyzerNotebookTag notebook_tag;
    SubGhzFrequencyAnalyzerMode mode;
    uint32_t preset_frequency;
    size_t preset_completed;
    size_t preset_total;
    size_t preset_selected_rank;
    size_t preset_result_count;
    bool preset_scan_complete;
    bool preset_result_valid;
    char preset_name[SUBGHZ_FREQUENCY_ANALYZER_PRESET_NAME_MAX];
    float preset_peak_rssi;
    float preset_average_rssi;
    float preset_noise_floor;
} SubGhzFrequencyAnalyzerModel;

void subghz_frequency_analyzer_set_callback(
    SubGhzFrequencyAnalyzer* subghz_frequency_analyzer,
    SubGhzFrequencyAnalyzerCallback callback,
    void* context) {
    furi_assert(subghz_frequency_analyzer);
    furi_assert(callback);
    subghz_frequency_analyzer->callback = callback;
    subghz_frequency_analyzer->context = context;
}

void subghz_frequency_analyzer_draw_rssi(
    Canvas* canvas,
    float rssi,
    float rssi_last,
    float trigger,
    uint8_t x,
    uint8_t y) {
    // Current RSSI
    if(!float_is_equal(rssi, 0.f)) {
        if(rssi > RSSI_MAX) {
            rssi = RSSI_MAX;
        }
        rssi = (rssi - RSSI_MIN) / RSSI_SCALE;
        uint8_t column_number = 0;
        for(size_t i = 0; i <= (uint8_t)rssi; i++) {
            if((i + 1) % 4) {
                column_number++;
                canvas_draw_box(canvas, x + 2 * i, (y + 4) - column_number, 2, column_number);
            }
        }
    }

    // Last RSSI
    if(!float_is_equal(rssi_last, 0.f)) {
        if(rssi_last > RSSI_MAX) {
            rssi_last = RSSI_MAX;
        }
        int max_x = (int)((rssi_last - RSSI_MIN) / RSSI_SCALE) * 2;
        //if(!(max_x % 8)) max_x -= 2;
        int max_h = (int)((rssi_last - RSSI_MIN) / RSSI_SCALE) + 1;
        max_h -= (max_h / 4) + 3;
        canvas_draw_line(canvas, x + max_x + 1, y - max_h, x + max_x + 1, y + 3);
    }

    // Trigger cursor
    trigger = (trigger - RSSI_MIN) / RSSI_SCALE;
    uint8_t tr_x = (uint8_t)((float)x + (2 * trigger));
    canvas_draw_dot(canvas, tr_x, y + 4);
    canvas_draw_line(canvas, tr_x - 1, y + 5, tr_x + 1, y + 5);

    canvas_draw_line(canvas, x, y + 3, x + (RSSI_MAX - RSSI_MIN) * 2 / RSSI_SCALE, y + 3);
}

static void subghz_frequency_analyzer_history_frequency_draw(
    Canvas* canvas,
    SubGhzFrequencyAnalyzerModel* model) {
    char buffer[64] = {0};
    const uint8_t x1 = 2;
    const uint8_t x2 = 66;
    const uint8_t y = 37;

    canvas_set_font(canvas, FontSecondary);
    uint8_t line = 0;
    bool show_frame = model->show_frame && model->max_index > 0;
    for(uint8_t i = 0; i < MAX_HISTORY; i++) {
        uint8_t current_x;
        uint8_t current_y = y + line * 11;

        if(i % 2 == 0) {
            current_x = x1;
        } else {
            current_x = x2;
            line++;
        }
        if(model->history_frequency[i]) {
            snprintf(
                buffer,
                sizeof(buffer),
                "%03ld.%03ld",
                model->history_frequency[i] / 1000000 % 1000,
                model->history_frequency[i] / 1000 % 1000);
            canvas_draw_str(canvas, current_x, current_y, buffer);
        } else {
            canvas_draw_str(canvas, current_x, current_y, "---.---");
        }
        if(model->history_frequency_rx_count[i] > 0) {
            snprintf(buffer, sizeof(buffer), "x%d", model->history_frequency_rx_count[i]);
            canvas_draw_str(canvas, current_x + 41, current_y, buffer);
        } else if(model->history_frequency[i]) {
            canvas_draw_str(canvas, current_x + 41, current_y, "MHz");
        }

        if(show_frame && i == model->selected_index) {
            elements_frame(canvas, current_x - 2, current_y - 9, 63, 11);
        }
    }
}

static void subghz_frequency_analyzer_draw_top_button(
    Canvas* canvas,
    int32_t x,
    size_t width,
    const char* label,
    const Icon* icon) {
    const int32_t y = 1;
    const size_t height = 9;
    canvas_draw_box(canvas, x, y, width, height);
    canvas_invert_color(canvas);

    if(icon) {
        const size_t content_width = icon_get_width(icon) + 2 + canvas_string_width(canvas, label);
        const int32_t content_x = x + ((int32_t)width - (int32_t)content_width) / 2;
        const int32_t icon_y = y + ((int32_t)height - (int32_t)icon_get_height(icon)) / 2;
        canvas_draw_icon(canvas, content_x, icon_y, icon);
        canvas_draw_str_aligned(
            canvas, content_x + icon_get_width(icon) + 2, 6, AlignLeft, AlignCenter, label);
    } else {
        canvas_draw_str_aligned(canvas, x + width / 2, 6, AlignCenter, AlignCenter, label);
    }
    canvas_invert_color(canvas);
}

static void subghz_frequency_analyzer_draw_action_hint(Canvas* canvas) {
    subghz_frequency_analyzer_draw_top_button(canvas, 2, 41, "Save", &I_ButtonCenter_7x7);
    subghz_frequency_analyzer_draw_top_button(canvas, 45, 42, "Scan", &I_ButtonUp_7x4);
    subghz_frequency_analyzer_draw_top_button(canvas, 96, 30, "RX", &I_ButtonCenter_7x7);
}

static int16_t subghz_frequency_analyzer_dbm_to_int(float dbm) {
    return dbm >= 0.0f ? (int16_t)(dbm + 0.5f) : (int16_t)(dbm - 0.5f);
}

static void subghz_frequency_analyzer_draw_preset_scan(
    Canvas* canvas,
    SubGhzFrequencyAnalyzerModel* model) {
    char buffer[64];
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    subghz_frequency_analyzer_draw_top_button(canvas, 2, 42, "Freq", &I_ButtonUp_7x4);
    subghz_frequency_analyzer_draw_top_button(canvas, 91, 35, "Scan", &I_ButtonDown_7x4);

    snprintf(
        buffer,
        sizeof(buffer),
        "%03ld.%03ld MHz",
        model->preset_frequency / 1000000 % 1000,
        model->preset_frequency / 1000 % 1000);
    canvas_draw_str(canvas, 2, 21, buffer);
    canvas_draw_str_aligned(
        canvas, 126, 21, AlignRight, AlignBottom, model->is_ext_radio ? "EXT" : "INT");
    canvas_draw_line(canvas, 2, 24, 125, 24);

    if(!model->preset_scan_complete) {
        snprintf(
            buffer,
            sizeof(buffer),
            "Scanning %lu/%lu",
            (unsigned long)model->preset_completed,
            (unsigned long)model->preset_total);
        canvas_draw_str(canvas, 4, 36, buffer);
        canvas_draw_str_aligned(
            canvas,
            124,
            36,
            AlignRight,
            AlignBottom,
            model->preset_result_valid ? model->preset_name : "Preparing");
        elements_frame(canvas, 8, 43, 112, 8);
        if(model->preset_total > 0) {
            const size_t width = (108U * model->preset_completed) / model->preset_total;
            if(width > 0) canvas_draw_box(canvas, 10, 45, width, 4);
        }
        canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, "Back cancels");
        return;
    }

    if(!model->preset_result_valid) {
        canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignBottom, "No presets available");
        elements_button_center(canvas, "Scan");
        return;
    }

    snprintf(
        buffer,
        sizeof(buffer),
        "%lu/%lu  %s",
        (unsigned long)(model->preset_selected_rank + 1),
        (unsigned long)model->preset_result_count,
        model->preset_name);
    canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignBottom, buffer);
    snprintf(
        buffer,
        sizeof(buffer),
        "A %d  P %d  N %d dBm",
        subghz_frequency_analyzer_dbm_to_int(model->preset_average_rssi),
        subghz_frequency_analyzer_dbm_to_int(model->preset_peak_rssi),
        subghz_frequency_analyzer_dbm_to_int(model->preset_noise_floor));
    canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignBottom, buffer);
    elements_button_left(canvas, "Prev");
    elements_button_center(canvas, "RX");
    elements_button_right(canvas, "Next");
}

void subghz_frequency_analyzer_draw(Canvas* canvas, SubGhzFrequencyAnalyzerModel* model) {
    char buffer[64] = {0};

    if(model->mode == SubGhzFrequencyAnalyzerModePreset) {
        subghz_frequency_analyzer_draw_preset_scan(canvas, model);
        return;
    }

    // Action hint: short OK saves/logs, long OK opens Receiver on the selected frequency.
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    subghz_frequency_analyzer_draw_action_hint(canvas);

    // RSSI
    canvas_draw_str(canvas, 33, 62, "RSSI");
    subghz_frequency_analyzer_draw_rssi(
        canvas, model->rssi, model->rssi_last, model->trigger, 56, 57);

    // Last detected frequency
    subghz_frequency_analyzer_history_frequency_draw(canvas, model);

    // Frequency
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(
        buffer,
        sizeof(buffer),
        "%03ld.%03ld",
        model->frequency / 1000000 % 1000,
        model->frequency / 1000 % 1000);
    if(model->signal) {
        canvas_draw_box(canvas, 4, 10, 121, 19);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorBlack);
    }

    canvas_draw_str(canvas, 8, 26, buffer);
    canvas_draw_icon(canvas, 96, 15, &I_MHz_25x11);

    canvas_set_color(canvas, ColorBlack);

    // Buttons hint
    canvas_set_font(canvas, FontSecondary);
    elements_button_left(canvas, "T-");
    elements_button_right(canvas, "+T");
}

static int16_t subghz_frequency_analyzer_dbm_to_x10(float dbm) {
    if(dbm >= 0.0f) {
        return (int16_t)(dbm * 10.0f + 0.5f);
    }

    return (int16_t)(dbm * 10.0f - 0.5f);
}

static bool subghz_frequency_analyzer_prepare_observation(
    SubGhzFrequencyAnalyzer* instance,
    SubGhzFrequencyAnalyzerModel* model,
    SubGhzFrequencyAnalyzerObservation* observation) {
    furi_assert(instance);
    furi_assert(model);
    furi_assert(observation);

    memset(observation, 0, sizeof(SubGhzFrequencyAnalyzerObservation));
    observation->trigger_dbm_x10 = subghz_frequency_analyzer_dbm_to_x10(model->trigger);
    observation->is_ext_radio = model->is_ext_radio;
    observation->signal = model->signal;
    observation->notebook_tag = model->notebook_tag;

    uint32_t frequency_candidate = 0;
    if(model->show_frame && !model->signal && model->selected_index < MAX_HISTORY) {
        frequency_candidate = model->history_frequency[model->selected_index];
        observation->source = SubGhzFrequencyAnalyzerObservationSourceHistory;
        observation->history_index = model->selected_index + 1;
        observation->rx_count = model->history_frequency_rx_count[model->selected_index] > 0 ?
                                    model->history_frequency_rx_count[model->selected_index] :
                                    1;
        observation->rssi_dbm_x10 = subghz_frequency_analyzer_dbm_to_x10(model->rssi_last);
    } else if(model->signal && model->frequency > 0) {
        frequency_candidate =
            subghz_frequency_analyzer_get_nearest_frequency(instance->worker, model->frequency);
        observation->source = SubGhzFrequencyAnalyzerObservationSourceLive;
        observation->rx_count = 1;
        observation->rssi_dbm_x10 = subghz_frequency_analyzer_dbm_to_x10(model->rssi);
    }

    if(frequency_candidate == 0) {
        return false;
    }

    frequency_candidate =
        subghz_frequency_analyzer_get_nearest_frequency(instance->worker, frequency_candidate);
    if(!subghz_txrx_radio_device_is_frequency_valid(instance->txrx, frequency_candidate)) {
        return false;
    }

    observation->frequency = frequency_candidate;
    observation->valid = true;
    return true;
}

static bool
    subghz_frequency_analyzer_show_preset_rank(SubGhzFrequencyAnalyzer* instance, size_t rank) {
    SubGhzFrequencyAnalyzerPresetResult result;
    if(!subghz_frequency_analyzer_worker_get_preset_result(instance->worker, rank, &result)) {
        return false;
    }

    instance->preset_selected_rank = rank;
    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        {
            model->preset_selected_rank = rank;
            model->preset_result_count = instance->preset_result_count;
            model->preset_result_valid = true;
            strlcpy(model->preset_name, result.preset_name, sizeof(model->preset_name));
            model->preset_peak_rssi = result.peak_rssi;
            model->preset_average_rssi = result.average_rssi;
            model->preset_noise_floor = result.noise_floor;
        },
        true);
    return true;
}

static void subghz_frequency_analyzer_preset_callback(
    void* context,
    const SubGhzFrequencyAnalyzerPresetProgress* progress) {
    SubGhzFrequencyAnalyzer* instance = context;
    furi_assert(instance);
    furi_assert(progress);

    if(progress->complete) {
        instance->preset_scan_complete = true;
        instance->preset_result_count =
            subghz_frequency_analyzer_worker_get_preset_result_count(instance->worker);
        instance->preset_selected_rank = 0;
    }

    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        {
            model->preset_frequency = progress->frequency;
            model->preset_completed = progress->completed;
            model->preset_total = progress->total;
            model->preset_scan_complete = progress->complete;
            model->preset_result_count = instance->preset_result_count;
            model->preset_selected_rank = instance->preset_selected_rank;
            model->preset_result_valid = progress->valid;
            model->is_ext_radio = subghz_txrx_radio_device_get(instance->txrx) !=
                                  SubGhzRadioDeviceTypeInternal;
            if(progress->valid) {
                strlcpy(
                    model->preset_name, progress->result.preset_name, sizeof(model->preset_name));
                model->preset_peak_rssi = progress->result.peak_rssi;
                model->preset_average_rssi = progress->result.average_rssi;
                model->preset_noise_floor = progress->result.noise_floor;
            }
        },
        true);

    if(progress->complete && instance->preset_result_count > 0) {
        subghz_frequency_analyzer_show_preset_rank(instance, 0);
    }
}

static void subghz_frequency_analyzer_start_preset_scan(
    SubGhzFrequencyAnalyzer* instance,
    uint32_t frequency) {
    if(subghz_frequency_analyzer_worker_is_running(instance->worker)) {
        subghz_frequency_analyzer_worker_stop(instance->worker);
    }

    instance->mode = SubGhzFrequencyAnalyzerModePreset;
    instance->preset_frequency = frequency;
    instance->preset_scan_complete = false;
    instance->preset_result_count = 0;
    instance->preset_selected_rank = 0;
    subghz_frequency_analyzer_worker_set_mode(
        instance->worker, SubGhzFrequencyAnalyzerWorkerModePreset, frequency);
    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        {
            model->mode = SubGhzFrequencyAnalyzerModePreset;
            model->preset_frequency = frequency;
            model->preset_completed = 0;
            model->preset_total = 0;
            model->preset_selected_rank = 0;
            model->preset_result_count = 0;
            model->preset_scan_complete = false;
            model->preset_result_valid = false;
            model->preset_name[0] = '\0';
        },
        true);
    subghz_frequency_analyzer_worker_start(instance->worker);
}

static void subghz_frequency_analyzer_start_frequency_scan(SubGhzFrequencyAnalyzer* instance) {
    if(subghz_frequency_analyzer_worker_is_running(instance->worker)) {
        subghz_frequency_analyzer_worker_stop(instance->worker);
    }
    instance->mode = SubGhzFrequencyAnalyzerModeFrequency;
    subghz_frequency_analyzer_worker_set_mode(
        instance->worker, SubGhzFrequencyAnalyzerWorkerModeFrequency, 0);
    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        { model->mode = SubGhzFrequencyAnalyzerModeFrequency; },
        true);
    subghz_frequency_analyzer_worker_start(instance->worker);
}

static uint32_t subghz_frequency_analyzer_get_scan_frequency(SubGhzFrequencyAnalyzer* instance) {
    uint32_t frequency = subghz_txrx_get_preset(instance->txrx).frequency;
    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        {
            SubGhzFrequencyAnalyzerObservation observation;
            if(subghz_frequency_analyzer_prepare_observation(instance, model, &observation)) {
                frequency = observation.frequency;
            }
        },
        false);
    return frequency;
}

bool subghz_frequency_analyzer_input(InputEvent* event, void* context) {
    furi_assert(context);
    SubGhzFrequencyAnalyzer* instance = (SubGhzFrequencyAnalyzer*)context;

    bool need_redraw = false;
    if(event->key == InputKeyBack) {
        return need_redraw;
    }

    if(event->type == InputTypeShort && event->key == InputKeyUp) {
        if(instance->mode == SubGhzFrequencyAnalyzerModePreset) {
            subghz_frequency_analyzer_start_frequency_scan(instance);
        } else {
            subghz_frequency_analyzer_start_preset_scan(
                instance, subghz_frequency_analyzer_get_scan_frequency(instance));
        }
        return true;
    }

    if(instance->mode == SubGhzFrequencyAnalyzerModePreset) {
        if(event->type == InputTypeShort && event->key == InputKeyDown) {
            subghz_frequency_analyzer_start_preset_scan(instance, instance->preset_frequency);
        } else if(
            instance->preset_scan_complete && instance->preset_result_count == 0 &&
            event->type == InputTypeShort && event->key == InputKeyOk) {
            subghz_frequency_analyzer_start_preset_scan(instance, instance->preset_frequency);
        } else if(
            instance->preset_scan_complete && instance->preset_result_count > 0 &&
            (event->type == InputTypePress || event->type == InputTypeRepeat) &&
            (event->key == InputKeyLeft || event->key == InputKeyRight)) {
            size_t rank = instance->preset_selected_rank;
            if(event->key == InputKeyLeft) {
                rank = rank == 0 ? instance->preset_result_count - 1 : rank - 1;
            } else {
                rank = (rank + 1) % instance->preset_result_count;
            }
            subghz_frequency_analyzer_show_preset_rank(instance, rank);
        } else if(
            instance->preset_scan_complete && instance->preset_result_count > 0 &&
            (event->type == InputTypeShort || event->type == InputTypeLong) &&
            event->key == InputKeyOk) {
            instance->callback(SubGhzCustomEventViewFreqAnalPresetRx, instance->context);
        } else if(event->type == InputTypeLong && event->key == InputKeyUp) {
            if(instance->feedback_level == SubGHzFrequencyAnalyzerFeedbackLevelAll) {
                instance->feedback_level = SubGHzFrequencyAnalyzerFeedbackLevelMute;
            } else {
                instance->feedback_level--;
            }
        }
        return true;
    }

    bool is_press_or_repeat = (event->type == InputTypePress) || (event->type == InputTypeRepeat);
    if(is_press_or_repeat && (event->key == InputKeyLeft || event->key == InputKeyRight)) {
        // Trigger setup
        float trigger_level = subghz_frequency_analyzer_worker_get_trigger_level(instance->worker);
        if(event->key == InputKeyLeft) {
            trigger_level -= TRIGGER_STEP;
            if(trigger_level < RSSI_MIN) {
                trigger_level = RSSI_MIN;
            }
        } else {
            trigger_level += TRIGGER_STEP;
            if(trigger_level > RSSI_MAX) {
                trigger_level = RSSI_MAX;
            }
        }
        subghz_frequency_analyzer_worker_set_trigger_level(instance->worker, trigger_level);
        FURI_LOG_D(TAG, "trigger = %.1f", (double)trigger_level);
        need_redraw = true;
    } else if(event->type == InputTypeLong && event->key == InputKeyUp) {
        if(instance->feedback_level == SubGHzFrequencyAnalyzerFeedbackLevelAll) {
            instance->feedback_level = SubGHzFrequencyAnalyzerFeedbackLevelMute;
        } else {
            instance->feedback_level--;
        }

        need_redraw = true;
    } else if(is_press_or_repeat && event->key == InputKeyDown) {
        instance->show_frame = instance->max_index > 0;
        if(instance->show_frame) {
            instance->selected_index = (instance->selected_index + 1) % instance->max_index;
            need_redraw = true;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyDown) {
        instance->notebook_tag =
            (instance->notebook_tag + 1) % SubGhzFrequencyAnalyzerNotebookTagCount;
        need_redraw = true;
    } else if(
        (event->type == InputTypeShort || event->type == InputTypeLong) &&
        event->key == InputKeyOk) {
        need_redraw = false;
        bool observation_valid = false;
        uint32_t frequency_to_save = 0;
        with_view_model(
            instance->view,
            SubGhzFrequencyAnalyzerModel * model,
            {
                SubGhzFrequencyAnalyzerObservation observation;
                observation_valid =
                    subghz_frequency_analyzer_prepare_observation(instance, model, &observation);
                if(observation_valid) {
                    model->frequency_to_save = observation.frequency;
                    instance->last_observation = observation;
                    frequency_to_save = observation.frequency;
                } else {
                    instance->last_observation.valid = false;
                }
            },
            false);

        if(observation_valid) {
            instance->callback(SubGhzCustomEventViewFreqAnalOkShort, instance->context);
        }

        // If it was a long press also send a second event
        if(event->type == InputTypeLong && frequency_to_save > 0) {
            // Worker stopped on app thread instead of GUI thread when switching scene in callback

            instance->callback(SubGhzCustomEventViewFreqAnalOkLong, instance->context);
        }
    }

    if(need_redraw) {
        with_view_model(
            instance->view,
            SubGhzFrequencyAnalyzerModel * model,
            {
                model->rssi_last = instance->rssi_last;
                model->trigger =
                    subghz_frequency_analyzer_worker_get_trigger_level(instance->worker);
                model->feedback_level = instance->feedback_level;
                model->max_index = instance->max_index;
                model->show_frame = instance->show_frame;
                model->selected_index = instance->selected_index;
                model->notebook_tag = instance->notebook_tag;
            },
            true);
    }

    return true;
}

uint32_t round_int(uint32_t value, uint8_t n) {
    // Round value
    uint8_t on = n;
    while(n--) {
        uint8_t i = value % 10;
        value /= 10;
        if(i >= 5) value++;
    }
    while(on--)
        value *= 10;
    return value;
}

void subghz_frequency_analyzer_pair_callback(
    void* context,
    uint32_t frequency,
    float rssi,
    bool signal) {
    SubGhzFrequencyAnalyzer* instance = (SubGhzFrequencyAnalyzer*)context;
    if(float_is_equal(rssi, 0.f) && instance->locked) {
        if(instance->callback) {
            instance->callback(SubGhzCustomEventSceneAnalyzerUnlock, instance->context);
        }
        //update history
        instance->show_frame = true;
        uint8_t max_index = instance->max_index;
        with_view_model(
            instance->view,
            SubGhzFrequencyAnalyzerModel * model,
            {
                bool in_array = false;
                uint32_t normal_frequency = subghz_frequency_analyzer_get_nearest_frequency(
                    instance->worker, model->frequency);
                for(size_t i = 0; i < MAX_HISTORY; i++) {
                    if(model->history_frequency[i] == normal_frequency) {
                        in_array = true;
                        if(model->history_frequency[i] > 0) {
                            if(model->history_frequency_rx_count[i] == 0) {
                                model->history_frequency_rx_count[i]++;
                            }
                            model->history_frequency_rx_count[i]++;
                        }
                        if(i > 0) {
                            size_t offset = 0;
                            uint8_t temp_rx_count = model->history_frequency_rx_count[i];

                            for(size_t j = MAX_HISTORY - 1; j > 0; j--) {
                                if(j == i) {
                                    offset++;
                                }
                                model->history_frequency[j] = model->history_frequency[j - offset];
                                model->history_frequency_rx_count[j] =
                                    model->history_frequency_rx_count[j - offset];
                            }
                            model->history_frequency[0] = normal_frequency;
                            model->history_frequency_rx_count[0] = temp_rx_count;
                        }

                        break;
                    }
                }

                if(!in_array) {
                    model->history_frequency[3] = model->history_frequency[2];
                    model->history_frequency[2] = model->history_frequency[1];
                    model->history_frequency[1] = model->history_frequency[0];
                    model->history_frequency[0] = normal_frequency;

                    model->history_frequency_rx_count[3] = model->history_frequency_rx_count[2];
                    model->history_frequency_rx_count[2] = model->history_frequency_rx_count[1];
                    model->history_frequency_rx_count[1] = model->history_frequency_rx_count[0];
                    model->history_frequency_rx_count[0] = 0;
                }

                if(max_index < MAX_HISTORY) {
                    for(size_t i = 0; i < MAX_HISTORY; i++) {
                        if(model->history_frequency[i] > 0) {
                            max_index = i + 1;
                        }
                    }
                }
            },
            false);
        instance->max_index = max_index;
    } else if(!float_is_equal(rssi, 0.f) && !instance->locked) {
        // There is some signal
        FURI_LOG_I(TAG, "rssi = %.2f, frequency = %ld Hz", (double)rssi, frequency);
        frequency = round_int(frequency, 3); // Round 299999990Hz to 300000000Hz

        // Triggered!
        instance->rssi_last = rssi;
        if(instance->callback) {
            instance->callback(SubGhzCustomEventSceneAnalyzerLock, instance->context);
        }
    }

    // Update values
    if(rssi >= instance->rssi_last && frequency != 0) {
        instance->rssi_last = rssi;
    }

    instance->locked = !float_is_equal(rssi, 0.f);
    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        {
            model->rssi = rssi;
            model->rssi_last = instance->rssi_last;
            model->frequency = frequency;
            model->signal = signal;
            model->trigger = subghz_frequency_analyzer_worker_get_trigger_level(instance->worker);
            model->feedback_level = instance->feedback_level;
            model->max_index = instance->max_index;
            model->show_frame = instance->show_frame;
            model->selected_index = instance->selected_index;
            model->notebook_tag = instance->notebook_tag;
        },
        true);
}

void subghz_frequency_analyzer_enter(void* context) {
    furi_assert(context);
    SubGhzFrequencyAnalyzer* instance = (SubGhzFrequencyAnalyzer*)context;

    //Start worker
    instance->worker = subghz_frequency_analyzer_worker_alloc(instance->context);

    subghz_frequency_analyzer_worker_set_pair_callback(
        instance->worker,
        (SubGhzFrequencyAnalyzerWorkerPairCallback)subghz_frequency_analyzer_pair_callback,
        instance);
    subghz_frequency_analyzer_worker_set_preset_callback(
        instance->worker, subghz_frequency_analyzer_preset_callback, instance);

    instance->rssi_last = 0;
    instance->selected_index = 0;
    instance->max_index = 0;
    instance->show_frame = false;
    instance->notebook_tag = SubGhzFrequencyAnalyzerNotebookTagField;
    instance->mode = SubGhzFrequencyAnalyzerModeFrequency;
    instance->preset_scan_complete = false;
    instance->preset_result_count = 0;
    instance->preset_selected_rank = 0;
    //subghz_frequency_analyzer_worker_set_trigger_level(instance->worker, RSSI_MIN);

    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        {
            model->selected_index = 0;
            model->max_index = 0;
            model->show_frame = false;
            model->rssi = 0;
            model->rssi_last = 0;
            model->frequency = 0;
            model->history_frequency[3] = 0;
            model->history_frequency[2] = 0;
            model->history_frequency[1] = 0;
            model->history_frequency[0] = 0;
            model->history_frequency_rx_count[3] = 0;
            model->history_frequency_rx_count[2] = 0;
            model->history_frequency_rx_count[1] = 0;
            model->history_frequency_rx_count[0] = 0;
            model->frequency_to_save = 0;
            model->trigger = RSSI_MIN;
            model->is_ext_radio =
                (subghz_txrx_radio_device_get(instance->txrx) != SubGhzRadioDeviceTypeInternal);
            model->notebook_tag = instance->notebook_tag;
            model->mode = SubGhzFrequencyAnalyzerModeFrequency;
            model->preset_frequency = subghz_txrx_get_preset(instance->txrx).frequency;
            model->preset_completed = 0;
            model->preset_total = 0;
            model->preset_selected_rank = 0;
            model->preset_result_count = 0;
            model->preset_scan_complete = false;
            model->preset_result_valid = false;
            model->preset_name[0] = '\0';
        },
        true);
    subghz_frequency_analyzer_worker_start(instance->worker);
}

void subghz_frequency_analyzer_exit(void* context) {
    furi_assert(context);
    SubGhzFrequencyAnalyzer* instance = (SubGhzFrequencyAnalyzer*)context;

    // Stop worker
    if(subghz_frequency_analyzer_worker_is_running(instance->worker)) {
        subghz_frequency_analyzer_worker_stop(instance->worker);
    }
    subghz_frequency_analyzer_worker_free(instance->worker);

    furi_record_close(RECORD_NOTIFICATION);
}

SubGhzFrequencyAnalyzer* subghz_frequency_analyzer_alloc(SubGhzTxRx* txrx) {
    SubGhzFrequencyAnalyzer* instance = calloc(1, sizeof(SubGhzFrequencyAnalyzer));

    instance->feedback_level = SubGHzFrequencyAnalyzerFeedbackLevelMute;
    instance->notebook_tag = SubGhzFrequencyAnalyzerNotebookTagField;

    // View allocation and configuration
    instance->view = view_alloc();
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(SubGhzFrequencyAnalyzerModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, (ViewDrawCallback)subghz_frequency_analyzer_draw);
    view_set_input_callback(instance->view, subghz_frequency_analyzer_input);
    view_set_enter_callback(instance->view, subghz_frequency_analyzer_enter);
    view_set_exit_callback(instance->view, subghz_frequency_analyzer_exit);

    instance->txrx = txrx;

    return instance;
}

void subghz_frequency_analyzer_free(SubGhzFrequencyAnalyzer* instance) {
    furi_assert(instance);

    view_free(instance->view);
    free(instance);
}

View* subghz_frequency_analyzer_get_view(SubGhzFrequencyAnalyzer* instance) {
    furi_assert(instance);
    return instance->view;
}

uint32_t subghz_frequency_analyzer_get_frequency_to_save(SubGhzFrequencyAnalyzer* instance) {
    furi_assert(instance);
    uint32_t frequency;
    with_view_model(
        instance->view,
        SubGhzFrequencyAnalyzerModel * model,
        { frequency = model->frequency_to_save; },
        false);

    return frequency;
}

bool subghz_frequency_analyzer_get_observation(
    SubGhzFrequencyAnalyzer* instance,
    SubGhzFrequencyAnalyzerObservation* observation) {
    furi_assert(instance);
    furi_assert(observation);

    *observation = instance->last_observation;
    return observation->valid;
}

bool subghz_frequency_analyzer_get_selected_preset(
    SubGhzFrequencyAnalyzer* instance,
    uint32_t* frequency,
    uint32_t* preset_index) {
    furi_assert(instance);
    furi_assert(frequency);
    furi_assert(preset_index);

    SubGhzFrequencyAnalyzerPresetResult result;
    if(instance->mode != SubGhzFrequencyAnalyzerModePreset || !instance->preset_scan_complete ||
       !subghz_frequency_analyzer_worker_get_preset_result(
           instance->worker, instance->preset_selected_rank, &result)) {
        return false;
    }

    *frequency = instance->preset_frequency;
    *preset_index = result.preset_index;
    return true;
}

SubGHzFrequencyAnalyzerFeedbackLevel subghz_frequency_analyzer_feedback_level(
    SubGhzFrequencyAnalyzer* instance,
    SubGHzFrequencyAnalyzerFeedbackLevel level,
    bool update) {
    furi_assert(instance);
    if(update) {
        instance->feedback_level = level;
        with_view_model(
            instance->view,
            SubGhzFrequencyAnalyzerModel * model,
            { model->feedback_level = instance->feedback_level; },
            true);
    }

    return instance->feedback_level;
}

float subghz_frequency_analyzer_get_trigger_level(SubGhzFrequencyAnalyzer* instance) {
    furi_assert(instance);
    return subghz_frequency_analyzer_worker_get_trigger_level(instance->worker);
}
