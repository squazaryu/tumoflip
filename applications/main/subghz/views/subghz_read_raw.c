#include "subghz_read_raw.h"
#include "../subghz_i.h"

#include <furi.h>
#include <furi_hal.h>
#include <input/input.h>
#include <gui/elements.h>
#include <gui/icon.h>
#include <string.h>

#include <assets_icons.h>
#define SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE 100
#define TAG                               "SubGhzReadRaw"
#define SUBGHZ_RAW_SAVED_NAME_WIDTH       96U
#define SUBGHZ_RAW_SAVED_PRESET_WIDTH     37U
#define SUBGHZ_RAW_SAVED_TEXT_BUFFER      32U

struct SubGhzReadRAW {
    View* view;
    SubGhzReadRAWCallback callback;
    void* context;
};

typedef struct {
    FuriString* frequency_str;
    FuriString* preset_str;
    FuriString* sample_write;
    FuriString* file_name;
    uint8_t* rssi_history;
    uint8_t rssi_current;
    bool rssi_history_end;
    uint8_t ind_write;
    uint8_t ind_sin;
    SubGhzReadRAWStatus status;
    bool raw_send_only;
    float raw_threshold_rssi;
    bool not_showing_samples;
    SubGhzRadioDeviceType device_type;
} SubGhzReadRAWModel;

void subghz_read_raw_set_callback(
    SubGhzReadRAW* subghz_read_raw,
    SubGhzReadRAWCallback callback,
    void* context) {
    furi_assert(subghz_read_raw);
    furi_assert(callback);
    subghz_read_raw->callback = callback;
    subghz_read_raw->context = context;
}

void subghz_read_raw_add_data_statusbar(
    SubGhzReadRAW* instance,
    const char* frequency_str,
    const char* preset_str) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            furi_string_set(model->frequency_str, frequency_str);
            furi_string_set(model->preset_str, preset_str);
        },
        true);
}

void subghz_read_raw_set_radio_device_type(
    SubGhzReadRAW* instance,
    SubGhzRadioDeviceType device_type) {
    furi_assert(instance);
    with_view_model(
        instance->view, SubGhzReadRAWModel * model, { model->device_type = device_type; }, true);
}

void subghz_read_raw_add_data_rssi(SubGhzReadRAW* instance, float rssi, bool trace) {
    furi_assert(instance);
    uint8_t u_rssi = 0;

    if(rssi < SUBGHZ_RAW_THRESHOLD_MIN) {
        u_rssi = 0;
    } else {
        u_rssi = (uint8_t)((rssi - SUBGHZ_RAW_THRESHOLD_MIN) / 2.7f);
    }

    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            model->rssi_current = u_rssi;
            if(trace) {
                model->rssi_history[model->ind_write++] = u_rssi;
            } else {
                model->rssi_history[model->ind_write] = u_rssi;
            }

            if(model->ind_write >= SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE) {
                model->rssi_history_end = true;
                model->ind_write = 0;
            }
        },
        true);
}

void subghz_read_raw_update_sample_write(SubGhzReadRAW* instance, size_t sample) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            model->not_showing_samples = false;
            furi_string_printf(model->sample_write, "%zu spl.", sample);
        },
        false);
}

void subghz_read_raw_stop_send(SubGhzReadRAW* instance) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            switch(model->status) {
            case SubGhzReadRAWStatusTXRepeat:
            case SubGhzReadRAWStatusLoadKeyTXRepeat:
                instance->callback(SubGhzCustomEventViewReadRAWSendStart, instance->context);
                break;
            case SubGhzReadRAWStatusTX:
                model->status = SubGhzReadRAWStatusIDLE;
                break;
            case SubGhzReadRAWStatusLoadKeyTX:
                model->status = SubGhzReadRAWStatusLoadKeyIDLE;
                break;

            default:
                FURI_LOG_W(TAG, "unknown status");
                model->status = SubGhzReadRAWStatusIDLE;
                break;
            }
        },
        true);
}

void subghz_read_raw_update_sin(SubGhzReadRAW* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            if(model->ind_sin++ > 62) {
                model->ind_sin = 0;
            }
        },
        true);
}

static int8_t subghz_read_raw_tab_sin(uint8_t x) {
    const uint8_t tab_sin[64] = {0,   3,   6,   9,   12,  16,  19,  22,  25,  28,  31,  34,  37,
                                 40,  43,  46,  49,  51,  54,  57,  60,  63,  65,  68,  71,  73,
                                 76,  78,  81,  83,  85,  88,  90,  92,  94,  96,  98,  100, 102,
                                 104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120, 121,
                                 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127};

    int8_t r = tab_sin[((x & 0x40) ? -x - 1 : x) & 0x3f];
    if(x & 0x80) return -r;
    return r;
}

void subghz_read_raw_draw_sin(Canvas* canvas, SubGhzReadRAWModel* model) {
#define SUBGHZ_RAW_SIN_AMPLITUDE 11
    for(int i = 113; i > 0; i--) {
        canvas_draw_line(
            canvas,
            i,
            32 - subghz_read_raw_tab_sin(i + model->ind_sin * 16) / SUBGHZ_RAW_SIN_AMPLITUDE,
            i + 1,
            32 + subghz_read_raw_tab_sin((i + model->ind_sin * 16 + 1) * 2) /
                     SUBGHZ_RAW_SIN_AMPLITUDE);
        canvas_draw_line(
            canvas,
            i + 1,
            32 - subghz_read_raw_tab_sin(i + model->ind_sin * 16) / SUBGHZ_RAW_SIN_AMPLITUDE,
            i + 2,
            32 + subghz_read_raw_tab_sin((i + model->ind_sin * 16 + 1) * 2) /
                     SUBGHZ_RAW_SIN_AMPLITUDE);
    }
}

void subghz_read_raw_draw_scale(Canvas* canvas, SubGhzReadRAWModel* model) {
#define SUBGHZ_RAW_TOP_SCALE 14
#define SUBGHZ_RAW_END_SCALE 115

    if(model->rssi_history_end == false) {
        for(int i = SUBGHZ_RAW_END_SCALE; i > 0; i -= 15) {
            canvas_draw_line(canvas, i, SUBGHZ_RAW_TOP_SCALE, i, SUBGHZ_RAW_TOP_SCALE + 4);
            canvas_draw_line(canvas, i - 5, SUBGHZ_RAW_TOP_SCALE, i - 5, SUBGHZ_RAW_TOP_SCALE + 2);
            canvas_draw_line(
                canvas, i - 10, SUBGHZ_RAW_TOP_SCALE, i - 10, SUBGHZ_RAW_TOP_SCALE + 2);
        }
    } else {
        for(int i = SUBGHZ_RAW_END_SCALE - model->ind_write % 15; i > -15; i -= 15) {
            canvas_draw_line(canvas, i, SUBGHZ_RAW_TOP_SCALE, i, SUBGHZ_RAW_TOP_SCALE + 4);
            if(SUBGHZ_RAW_END_SCALE > i + 5)
                canvas_draw_line(
                    canvas, i + 5, SUBGHZ_RAW_TOP_SCALE, i + 5, SUBGHZ_RAW_TOP_SCALE + 2);
            if(SUBGHZ_RAW_END_SCALE > i + 10)
                canvas_draw_line(
                    canvas, i + 10, SUBGHZ_RAW_TOP_SCALE, i + 10, SUBGHZ_RAW_TOP_SCALE + 2);
        }
    }
}

void subghz_read_raw_draw_rssi(Canvas* canvas, SubGhzReadRAWModel* model) {
    int ind = 0;
    int base = 0;
    uint8_t width = 2;
    if(model->rssi_history_end == false) {
        for(int i = model->ind_write; i >= 0; i--) {
            canvas_draw_line(canvas, i, 47, i, 47 - model->rssi_history[i]);
        }
        canvas_draw_line(
            canvas, model->ind_write + 1, 47, model->ind_write + 1, 47 - model->rssi_current);
        if(model->ind_write > 3) {
            canvas_draw_line(
                canvas, model->ind_write - 1, 47, model->ind_write - 1, 47 - model->rssi_current);

            for(uint8_t i = 13; i < 47; i += width * 2) {
                canvas_draw_line(canvas, model->ind_write, i, model->ind_write, i + width);
            }
            canvas_draw_line(canvas, model->ind_write - 2, 12, model->ind_write + 2, 12);
            canvas_draw_line(canvas, model->ind_write - 1, 13, model->ind_write + 1, 13);
        }
    } else {
        int i = 0;
        base = SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE - model->ind_write;
        for(i = SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE; i > 0; i--) {
            ind = i - base;
            if(ind < 0) ind += SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE;
            canvas_draw_line(canvas, i, 47, i, 47 - model->rssi_history[ind]);
        }

        canvas_draw_line(
            canvas,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE - 1,
            47,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE - 1,
            47 - model->rssi_current);
        canvas_draw_line(
            canvas,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE + 1,
            47,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE + 1,
            47 - model->rssi_current);

        for(uint8_t i = 13; i < 47; i += width * 2) {
            canvas_draw_line(
                canvas,
                SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE,
                i,
                SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE,
                i + width);
        }
        canvas_draw_line(
            canvas,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE - 2,
            12,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE + 2,
            12);
        canvas_draw_line(
            canvas,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE - 1,
            13,
            SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE + 1,
            13);
    }
}

void subghz_read_raw_draw_threshold_rssi(Canvas* canvas, SubGhzReadRAWModel* model) {
    uint8_t x = 118;
    uint8_t y = 48;

    if(model->raw_threshold_rssi > SUBGHZ_RAW_THRESHOLD_MIN) {
        uint8_t x = 118;
        y -= (uint8_t)((model->raw_threshold_rssi - SUBGHZ_RAW_THRESHOLD_MIN) / 2.7f);

        uint8_t width = 3;
        for(uint8_t i = 0; i < x; i += width * 2) {
            canvas_draw_line(canvas, i, y, i + width, y);
        }
    }
    canvas_draw_line(canvas, x, y - 2, x, y + 2);
    canvas_draw_line(canvas, x - 1, y - 1, x - 1, y + 1);
    canvas_draw_dot(canvas, x - 2, y);
}

static void subghz_read_raw_fit_text(
    Canvas* canvas,
    char* display_text,
    size_t display_text_size,
    const char* source_text,
    uint16_t max_width) {
    bool truncated = strlcpy(display_text, source_text, display_text_size) >= display_text_size;

    while(display_text[0] && (canvas_string_width(canvas, display_text) > max_width)) {
        display_text[strlen(display_text) - 1U] = '\0';
        truncated = true;
    }

    if(truncated) {
        const uint16_t ellipsis_width = canvas_string_width(canvas, "...");
        while(display_text[0] &&
              (canvas_string_width(canvas, display_text) + ellipsis_width > max_width)) {
            display_text[strlen(display_text) - 1U] = '\0';
        }
        strlcat(display_text, "...", display_text_size);
    }
}

static void subghz_read_raw_draw_saved_header(Canvas* canvas, SubGhzReadRAWModel* model) {
    char display_name[SUBGHZ_RAW_SAVED_TEXT_BUFFER];

    canvas_set_font(canvas, FontPrimary);
    subghz_read_raw_fit_text(
        canvas,
        display_name,
        sizeof(display_name),
        furi_string_get_cstr(model->file_name),
        SUBGHZ_RAW_SAVED_NAME_WIDTH);
    canvas_draw_str(canvas, 2, 10, display_name);

    canvas_draw_rbox(canvas, 103, 0, 25, 12, 2);
    canvas_invert_color(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 108, 8, "RAW");
    canvas_invert_color(canvas);

    canvas_draw_line(canvas, 0, 14, 127, 14);
}

static void subghz_read_raw_draw_saved_details(Canvas* canvas, SubGhzReadRAWModel* model) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 26, "FREQ");

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 31, 27, furi_string_get_cstr(model->frequency_str));
    const uint16_t frequency_end =
        31U + canvas_string_width(canvas, furi_string_get_cstr(model->frequency_str));

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, frequency_end + 2U, 26, "MHz");
    canvas_draw_line(canvas, 2, 32, 126, 32);

    canvas_draw_str(canvas, 2, 44, "MOD");
    canvas_set_font(canvas, FontPrimary);
    char display_preset[SUBGHZ_RAW_SAVED_TEXT_BUFFER];
    subghz_read_raw_fit_text(
        canvas,
        display_preset,
        sizeof(display_preset),
        furi_string_get_cstr(model->preset_str),
        SUBGHZ_RAW_SAVED_PRESET_WIDTH);
    canvas_draw_str(canvas, 31, 45, display_preset);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 71, 44, "RADIO");
    canvas_draw_rbox(canvas, 104, 34, 24, 13, 2);
    canvas_invert_color(canvas);
    canvas_draw_str(
        canvas, 109, 43, (model->device_type == SubGhzRadioDeviceTypeInternal) ? "INT" : "EXT");
    canvas_invert_color(canvas);
}

static void
    subghz_read_raw_draw_saved_center_action(Canvas* canvas, const char* label, bool filled) {
    canvas_set_font(canvas, FontSecondary);
    const uint16_t icon_width = icon_get_width(&I_ButtonCenter_7x7);
    const uint16_t content_width = icon_width + 4U + canvas_string_width(canvas, label);
    uint16_t button_width = content_width + 8U;
    if(button_width < 38U) button_width = 38U;

    const int32_t button_x = (128 - button_width) / 2;
    const int32_t content_x = button_x + (button_width - content_width) / 2;

    if(filled) {
        canvas_draw_rbox(canvas, button_x, 52, button_width, 12, 2);
        canvas_invert_color(canvas);
    } else {
        canvas_draw_rframe(canvas, button_x, 52, button_width, 12, 2);
    }

    canvas_draw_icon(canvas, content_x, 55, &I_ButtonCenter_7x7);
    canvas_draw_str(canvas, content_x + icon_width + 4U, 61, label);

    if(filled) canvas_invert_color(canvas);
}

static void subghz_read_raw_draw_saved_actions(Canvas* canvas, bool raw_send_only) {
    canvas_set_font(canvas, FontSecondary);

    if(!raw_send_only) {
        canvas_draw_icon(canvas, 3, 55, &I_ButtonLeft_4x7);
        canvas_draw_str(canvas, 11, 62, "New");

        const uint16_t more_width = canvas_string_width(canvas, "More");
        canvas_draw_str(canvas, 117 - more_width, 62, "More");
        canvas_draw_icon(canvas, 121, 55, &I_ButtonRight_4x7);
    }

    subghz_read_raw_draw_saved_center_action(canvas, "Send", true);
}

static bool subghz_read_raw_is_transmitting(SubGhzReadRAWStatus status) {
    return (status == SubGhzReadRAWStatusTX) || (status == SubGhzReadRAWStatusTXRepeat) ||
           (status == SubGhzReadRAWStatusLoadKeyTX) ||
           (status == SubGhzReadRAWStatusLoadKeyTXRepeat);
}

static void subghz_read_raw_draw_live_header(Canvas* canvas, SubGhzReadRAWModel* model) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 7, furi_string_get_cstr(model->frequency_str));
    canvas_draw_str(canvas, 35, 7, furi_string_get_cstr(model->preset_str));

    if(model->not_showing_samples) {
        canvas_draw_str(
            canvas,
            77,
            7,
            (model->device_type == SubGhzRadioDeviceTypeInternal) ? "R: Int" : "R: Ext");
    } else {
        canvas_draw_str(
            canvas, 70, 7, (model->device_type == SubGhzRadioDeviceTypeInternal) ? "I" : "E");
    }

    canvas_draw_str_aligned(
        canvas, 126, 0, AlignRight, AlignTop, furi_string_get_cstr(model->sample_write));

    canvas_draw_line(canvas, 0, 14, 115, 14);
    canvas_draw_line(canvas, 0, 48, 115, 48);
    canvas_draw_line(canvas, 115, 14, 115, 48);
}

void subghz_read_raw_draw(Canvas* canvas, SubGhzReadRAWModel* model) {
    const bool is_saved_file = !furi_string_empty(model->file_name);
    const bool is_transmitting = subghz_read_raw_is_transmitting(model->status);

    canvas_set_color(canvas, ColorBlack);

    if(is_saved_file) {
        subghz_read_raw_draw_saved_header(canvas, model);
        if(!is_transmitting) {
            subghz_read_raw_draw_saved_details(canvas, model);
        }
    } else {
        subghz_read_raw_draw_live_header(canvas, model);
    }

    switch(model->status) {
    case SubGhzReadRAWStatusIDLE:
        elements_button_left(canvas, "Erase");
        elements_button_center(canvas, "Send");
        elements_button_right(canvas, "Save");
        break;
    case SubGhzReadRAWStatusLoadKeyIDLE:
        if(is_saved_file) {
            subghz_read_raw_draw_saved_actions(canvas, model->raw_send_only);
        } else {
            if(!model->raw_send_only) {
                elements_button_left(canvas, "New");
                elements_button_right(canvas, "More");
            }
            elements_button_center(canvas, "Send");
        }
        break;

    case SubGhzReadRAWStatusTX:
    case SubGhzReadRAWStatusTXRepeat:
    case SubGhzReadRAWStatusLoadKeyTX:
    case SubGhzReadRAWStatusLoadKeyTXRepeat:
        if(is_saved_file) {
            subghz_read_raw_draw_saved_center_action(canvas, "Hold to repeat", false);
        } else {
            elements_button_center(canvas, "Hold to repeat");
        }
        break;

    case SubGhzReadRAWStatusStart:
        elements_button_left(canvas, "Config");
        elements_button_center(canvas, "REC");
        break;

    default:
        elements_button_center(canvas, "Stop");
        break;
    }

    if(is_transmitting) {
        subghz_read_raw_draw_sin(canvas, model);
    } else if(!is_saved_file) {
        subghz_read_raw_draw_rssi(canvas, model);
        subghz_read_raw_draw_scale(canvas, model);
        subghz_read_raw_draw_threshold_rssi(canvas, model);
        canvas_set_font_direction(canvas, CanvasDirectionBottomToTop);
        canvas_draw_str(canvas, 128, 40, "RSSI");
        canvas_set_font_direction(canvas, CanvasDirectionLeftToRight);
    }
}

bool subghz_read_raw_input(InputEvent* event, void* context) {
    furi_assert(context);
    SubGhzReadRAW* instance = context;

    if((event->key == InputKeyOk) &&
       (event->type == InputTypeLong || event->type == InputTypeRepeat)) {
        //we check that if we hold the transfer button,
        //further check of events is not needed, we exit
        return false;
    } else if(event->key == InputKeyOk && event->type == InputTypePress) {
        uint8_t ret = false;
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                switch(model->status) {
                case SubGhzReadRAWStatusIDLE:
                    // Start TX
                    instance->callback(SubGhzCustomEventViewReadRAWSendStart, instance->context);
                    model->status = SubGhzReadRAWStatusTXRepeat;
                    ret = true;
                    break;
                case SubGhzReadRAWStatusTX:
                    // Start TXRepeat
                    model->status = SubGhzReadRAWStatusTXRepeat;
                    break;
                case SubGhzReadRAWStatusLoadKeyIDLE:
                    // Start Load Key TX
                    instance->callback(SubGhzCustomEventViewReadRAWSendStart, instance->context);
                    model->status = SubGhzReadRAWStatusLoadKeyTXRepeat;
                    ret = true;
                    break;
                case SubGhzReadRAWStatusLoadKeyTX:
                    // Start Load Key TXRepeat
                    model->status = SubGhzReadRAWStatusLoadKeyTXRepeat;
                    break;

                default:
                    break;
                }
            },
            ret);
    } else if(event->key == InputKeyOk && event->type == InputTypeRelease) {
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                if(model->status == SubGhzReadRAWStatusTXRepeat) {
                    // Stop repeat TX
                    model->status = SubGhzReadRAWStatusTX;
                } else if(model->status == SubGhzReadRAWStatusLoadKeyTXRepeat) {
                    // Stop repeat TX
                    model->status = SubGhzReadRAWStatusLoadKeyTX;
                }
            },
            false);
    } else if(event->key == InputKeyBack && event->type == InputTypeShort) {
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                switch(model->status) {
                case SubGhzReadRAWStatusREC:
                    //Stop REC
                    instance->callback(SubGhzCustomEventViewReadRAWIDLE, instance->context);
                    model->status = SubGhzReadRAWStatusIDLE;
                    break;
                case SubGhzReadRAWStatusLoadKeyTX:
                    //Stop TxRx
                    instance->callback(SubGhzCustomEventViewReadRAWTXRXStop, instance->context);
                    model->status = SubGhzReadRAWStatusLoadKeyIDLE;
                    break;
                case SubGhzReadRAWStatusTX:
                    //Stop TxRx
                    instance->callback(SubGhzCustomEventViewReadRAWTXRXStop, instance->context);
                    model->status = SubGhzReadRAWStatusIDLE;
                    break;
                case SubGhzReadRAWStatusLoadKeyIDLE:
                    //Exit
                    instance->callback(SubGhzCustomEventViewReadRAWBack, instance->context);
                    break;

                default:
                    //Exit
                    instance->callback(SubGhzCustomEventViewReadRAWBack, instance->context);
                    break;
                }
            },
            true);
    } else if(event->key == InputKeyLeft && event->type == InputTypeShort) {
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                if(!model->raw_send_only) {
                    if(model->status == SubGhzReadRAWStatusStart) {
                        //Config
                        instance->callback(SubGhzCustomEventViewReadRAWConfig, instance->context);
                    } else if(
                        (model->status == SubGhzReadRAWStatusIDLE) ||
                        (model->status == SubGhzReadRAWStatusLoadKeyIDLE)) {
                        //Erase
                        model->status = SubGhzReadRAWStatusStart;
                        model->rssi_history_end = false;
                        model->ind_write = 0;
                        model->not_showing_samples = true;
                        furi_string_set(model->sample_write, "0 spl.");
                        furi_string_reset(model->file_name);
                        instance->callback(SubGhzCustomEventViewReadRAWErase, instance->context);
                    }
                }
            },
            true);
    } else if(event->key == InputKeyRight && event->type == InputTypeShort) {
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                if(!model->raw_send_only) {
                    if(model->status == SubGhzReadRAWStatusIDLE) {
                        //Save
                        instance->callback(SubGhzCustomEventViewReadRAWSave, instance->context);
                    } else if(model->status == SubGhzReadRAWStatusLoadKeyIDLE) {
                        //More
                        instance->callback(SubGhzCustomEventViewReadRAWMore, instance->context);
                    }
                }
            },
            true);
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                if(model->status == SubGhzReadRAWStatusStart) {
                    //Record
                    instance->callback(SubGhzCustomEventViewReadRAWREC, instance->context);
                    model->status = SubGhzReadRAWStatusREC;
                    model->ind_write = 0;
                    model->rssi_history_end = false;
                } else if(model->status == SubGhzReadRAWStatusREC) {
                    //Stop
                    instance->callback(SubGhzCustomEventViewReadRAWIDLE, instance->context);
                    model->status = SubGhzReadRAWStatusIDLE;
                }
            },
            true);
    }
    return true;
}

void subghz_read_raw_set_status(
    SubGhzReadRAW* instance,
    SubGhzReadRAWStatus status,
    const char* file_name,
    float raw_threshold_rssi) {
    furi_assert(instance);

    switch(status) {
    case SubGhzReadRAWStatusStart:
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                model->status = SubGhzReadRAWStatusStart;
                model->rssi_history_end = false;
                model->ind_write = 0;
                model->not_showing_samples = true;
                furi_string_reset(model->file_name);
                furi_string_set(model->sample_write, "0 spl.");
                model->raw_threshold_rssi = raw_threshold_rssi;
            },
            true);
        break;
    case SubGhzReadRAWStatusIDLE:
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            { model->status = SubGhzReadRAWStatusIDLE; },
            true);
        break;
    case SubGhzReadRAWStatusLoadKeyTX:
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                model->status = SubGhzReadRAWStatusLoadKeyIDLE;
                model->rssi_history_end = false;
                model->ind_write = 0;
                model->not_showing_samples = true;
                furi_string_set(model->file_name, file_name);
                furi_string_set(model->sample_write, "RAW");
            },
            true);
        break;
    case SubGhzReadRAWStatusSaveKey:
        with_view_model(
            instance->view,
            SubGhzReadRAWModel * model,
            {
                model->status = SubGhzReadRAWStatusLoadKeyIDLE;
                if(!model->ind_write) {
                    model->not_showing_samples = true;
                    furi_string_set(model->file_name, file_name);
                    furi_string_set(model->sample_write, "RAW");
                } else {
                    furi_string_reset(model->file_name);
                }
            },
            true);
        break;

    default:
        FURI_LOG_W(TAG, "unknown status");
        break;
    }
}

void subghz_read_raw_enter(void* context) {
    furi_assert(context);
    //SubGhzReadRAW* instance = context;
}

void subghz_read_raw_exit(void* context) {
    furi_assert(context);
    SubGhzReadRAW* instance = context;

    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            if(model->status != SubGhzReadRAWStatusIDLE &&
               model->status != SubGhzReadRAWStatusStart &&
               model->status != SubGhzReadRAWStatusLoadKeyIDLE) {
                instance->callback(SubGhzCustomEventViewReadRAWIDLE, instance->context);
                model->status = SubGhzReadRAWStatusStart;
            }
        },
        true);
}

SubGhzReadRAW* subghz_read_raw_alloc(bool raw_send_only) {
    SubGhzReadRAW* instance = malloc(sizeof(SubGhzReadRAW));

    // View allocation and configuration
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzReadRAWModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, (ViewDrawCallback)subghz_read_raw_draw);
    view_set_input_callback(instance->view, subghz_read_raw_input);
    view_set_enter_callback(instance->view, subghz_read_raw_enter);
    view_set_exit_callback(instance->view, subghz_read_raw_exit);

    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            model->frequency_str = furi_string_alloc();
            model->preset_str = furi_string_alloc();
            model->sample_write = furi_string_alloc();
            model->file_name = furi_string_alloc();
            model->raw_send_only = raw_send_only;
            model->rssi_history = malloc(SUBGHZ_READ_RAW_RSSI_HISTORY_SIZE * sizeof(uint8_t));
            model->raw_threshold_rssi = -127.0f;
        },
        true);

    return instance;
}

void subghz_read_raw_free(SubGhzReadRAW* instance) {
    furi_assert(instance);

    with_view_model(
        instance->view,
        SubGhzReadRAWModel * model,
        {
            furi_string_free(model->frequency_str);
            furi_string_free(model->preset_str);
            furi_string_free(model->sample_write);
            furi_string_free(model->file_name);
            free(model->rssi_history);
        },
        true);
    view_free(instance->view);
    free(instance);
}

View* subghz_read_raw_get_view(SubGhzReadRAW* instance) {
    furi_assert(instance);
    return instance->view;
}
