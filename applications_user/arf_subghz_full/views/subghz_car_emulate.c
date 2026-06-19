#include "subghz_car_emulate.h"
#include "../helpers/subghz_custom_event.h"

#include <gui/elements.h>
#include <input/input.h>
#include <furi.h>

#define TAG "SubGhzCarEmulateView"

/* ── Model ──────────────────────────────────────────────────────────────── */
typedef struct {
    char     protocol_name[32];
    uint32_t serial;
    uint32_t counter;
    uint32_t original_counter;
    uint32_t freq;
    char     preset[12];
    bool     is_transmitting;
    uint8_t  anim_frame;
    char     label_ok[12];
    char     label_up[12];
    char     label_down[12];
    char     label_left[12];
    char     label_right[12];
} SubGhzCarEmulateViewModel;

/* ── Handle ─────────────────────────────────────────────────────────────── */
struct SubGhzCarEmulateView {
    View*                        view;
    SubGhzCarEmulateViewCallback callback;
    void*                        context;
};

/* ── Draw ───────────────────────────────────────────────────────────────── */
static void subghz_car_emulate_view_draw(Canvas* canvas, void* model_ptr) {
    SubGhzCarEmulateViewModel* m = model_ptr;

    m->anim_frame = (m->anim_frame + 1) % 8;

    canvas_clear(canvas);

    /* Header bar */
    canvas_draw_box(canvas, 0, 0, 128, 11);
    canvas_invert_color(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, m->protocol_name);
    canvas_invert_color(canvas);

    /* Info row 1: serial + counter */
    canvas_set_font(canvas, FontSecondary);
    char buf[32];

    if(m->serial <= 0xFFFFFFUL) {
        snprintf(buf, sizeof(buf), "SN:%06lX", (unsigned long)(m->serial & 0xFFFFFFUL));
    } else {
        snprintf(buf, sizeof(buf), "SN:%08lX", (unsigned long)m->serial);
    }
    canvas_draw_str(canvas, 2, 20, buf);

    snprintf(buf, sizeof(buf), "CNT:%04lX", (unsigned long)m->counter);
    canvas_draw_str(canvas, 68, 20, buf);

    if(m->counter > m->original_counter) {
        snprintf(buf, sizeof(buf), "+%ld", (long)(m->counter - m->original_counter));
        canvas_draw_str(canvas, 112, 20, buf);
    }

    /* Info row 2: frequency + preset */
    snprintf(
        buf,
        sizeof(buf),
        "F:%lu.%02lu",
        (unsigned long)(m->freq / 1000000UL),
        (unsigned long)((m->freq % 1000000UL) / 10000UL));
    canvas_draw_str(canvas, 2, 30, buf);
    canvas_draw_str(canvas, 95, 30, m->preset);

    /* ── Button labels ── */
    const uint8_t font_h = canvas_current_font_height(canvas);

    /* Centre  → UNLOCK (OK button) */
    {
        const char* lbl        = m->label_ok;
        uint8_t     w          = (uint8_t)(canvas_string_width(canvas, lbl) + 8U);
        canvas_draw_rbox(canvas, 64 - w / 2, 45 - font_h / 2, w, font_h, 3);
        canvas_invert_color(canvas);
        canvas_draw_str_aligned(canvas, 64, 49, AlignCenter, AlignBottom, lbl);
        canvas_invert_color(canvas);
    }

    /* Up → LOCK */
    {
        const char* lbl        = m->label_up;
        uint8_t     w          = (uint8_t)(canvas_string_width(canvas, lbl) + 8U);
        canvas_draw_rbox(canvas, 64 - w / 2, 33 - font_h / 2, w, font_h, 3);
        canvas_invert_color(canvas);
        canvas_draw_str_aligned(canvas, 64, 37, AlignCenter, AlignBottom, lbl);
        canvas_invert_color(canvas);
    }

    /* Left → PANIC */
    {
        const char* lbl = m->label_left;
        uint8_t     w   = (uint8_t)(canvas_string_width(canvas, lbl) + 8U);
        canvas_draw_rbox(canvas, 0, 46 - font_h / 2, w, font_h, 3);
        canvas_invert_color(canvas);
        canvas_draw_str_aligned(canvas, w / 2, 50, AlignCenter, AlignBottom, lbl);
        canvas_invert_color(canvas);
    }

    /* Right → generic extra */
    {
        const char* lbl = m->label_right;
        uint8_t     w   = (uint8_t)(canvas_string_width(canvas, lbl) + 8U);
        canvas_draw_rbox(canvas, 127 - w, 46 - font_h / 2, w, font_h, 3);
        canvas_invert_color(canvas);
        canvas_draw_str_aligned(canvas, 127 - w / 2, 50, AlignCenter, AlignBottom, lbl);
        canvas_invert_color(canvas);
    }

    /* Down → BOOT */
    {
        const char* lbl = m->label_down;
        uint8_t     w   = (uint8_t)(canvas_string_width(canvas, lbl) + 8U);
        canvas_draw_rbox(canvas, 64 - w / 2, 57 - font_h / 2, w, font_h, 3);
        canvas_invert_color(canvas);
        canvas_draw_str_aligned(canvas, 64, 61, AlignCenter, AlignBottom, lbl);
        canvas_invert_color(canvas);
    }

    /* TX overlay */
    if(m->is_transmitting) {
        canvas_draw_rbox(canvas, 24, 18, 80, 18, 3);
        canvas_invert_color(canvas);
        int wave = m->anim_frame % 3;
        canvas_draw_str(canvas, 28 + wave * 2, 25, ")))");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "TX");
        canvas_invert_color(canvas);
    }
}

/* ── Input ──────────────────────────────────────────────────────────────── */
static bool subghz_car_emulate_view_input(InputEvent* event, void* context) {
    SubGhzCarEmulateView* instance = context;
    furi_assert(instance);

    if(event->type == InputTypePress) {
        if(event->key == InputKeyBack) {
            if(instance->callback) {
                instance->callback(SubGhzCustomEventCarEmulateExit, instance->context);
            }
            return true;
        }

        /* Any directional / OK key → start TX */
        if(instance->callback) {
            /* Pack the raw InputKey into the upper bits of the event so the
               scene can read which button was pressed.
               Lower 16 bits = SubGhzCustomEventCarEmulateTransmit marker,
               upper 16 bits = InputKey value. */
            uint32_t ev = ((uint32_t)event->key << 16) |
                          (uint32_t)SubGhzCustomEventCarEmulateTransmit;
            instance->callback(ev, instance->context);
        }
        return true;

    } else if(event->type == InputTypeRelease) {
        if(event->key != InputKeyBack) {
            if(instance->callback) {
                instance->callback(SubGhzCustomEventCarEmulateStop, instance->context);
            }
            return true;
        }
    }

    return false;
}

/* ── Alloc / Free ───────────────────────────────────────────────────────── */
SubGhzCarEmulateView* subghz_car_emulate_view_alloc(void) {
    SubGhzCarEmulateView* instance = malloc(sizeof(SubGhzCarEmulateView));
    furi_check(instance);

    instance->view     = view_alloc();
    instance->callback = NULL;
    instance->context  = NULL;

    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(SubGhzCarEmulateViewModel));
    view_set_draw_callback(instance->view, subghz_car_emulate_view_draw);
    view_set_input_callback(instance->view, subghz_car_emulate_view_input);

    return instance;
}

void subghz_car_emulate_view_free(SubGhzCarEmulateView* instance) {
    furi_check(instance);
    view_free(instance->view);
    free(instance);
}

View* subghz_car_emulate_view_get_view(SubGhzCarEmulateView* instance) {
    furi_check(instance);
    return instance->view;
}

void subghz_car_emulate_view_set_callback(
    SubGhzCarEmulateView* instance,
    SubGhzCarEmulateViewCallback callback,
    void* context) {
    furi_check(instance);
    instance->callback = callback;
    instance->context  = context;
}

void subghz_car_emulate_view_set_data(
    SubGhzCarEmulateView* instance,
    const char*  protocol_name,
    uint32_t     serial,
    uint32_t     counter,
    uint32_t     original_counter,
    uint32_t     freq,
    const char*  preset,
    bool         is_transmitting) {
    furi_check(instance);

    with_view_model(
        instance->view,
        SubGhzCarEmulateViewModel * m,
        {
            strncpy(m->protocol_name, protocol_name, sizeof(m->protocol_name) - 1);
            m->protocol_name[sizeof(m->protocol_name) - 1] = '\0';
            m->serial           = serial;
            m->counter          = counter;
            m->original_counter = original_counter;
            m->freq             = freq;
            strncpy(m->preset, preset, sizeof(m->preset) - 1);
            m->preset[sizeof(m->preset) - 1] = '\0';
            m->is_transmitting  = is_transmitting;
        },
        true);
}

void subghz_car_emulate_view_set_labels(
    SubGhzCarEmulateView* instance,
    const char* ok,
    const char* up,
    const char* down,
    const char* left,
    const char* right) {
    furi_check(instance);
    with_view_model(
        instance->view,
        SubGhzCarEmulateViewModel * m,
        {
            strncpy(m->label_ok,    ok    ? ok    : "",  sizeof(m->label_ok)    - 1);
            strncpy(m->label_up,    up    ? up    : "",  sizeof(m->label_up)    - 1);
            strncpy(m->label_down,  down  ? down  : "",  sizeof(m->label_down)  - 1);
            strncpy(m->label_left,  left  ? left  : "",  sizeof(m->label_left)  - 1);
            strncpy(m->label_right, right ? right : "",  sizeof(m->label_right) - 1);
        },
        true);
}
