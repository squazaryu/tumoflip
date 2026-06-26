// views/protopirate_dual_receiver.c
#include "protopirate_dual_receiver.h"

#ifdef ENABLE_DUAL_RX_SCENE

#include "../protopirate_history.h"
#include <input/input.h>
#include <gui/elements.h>
#include <furi.h>

#define FRAME_HEIGHT             12
#define MENU_ITEMS               3u
#define MAX_LEN_PX               118
#define LIST_HEIGHT              (MENU_ITEMS * FRAME_HEIGHT) // 36
#define STATUS_TOP_Y             (LIST_HEIGHT) // 36
#define SUBGHZ_RAW_THRESHOLD_MIN -90.0f

struct ProtoPirateDualReceiver {
    View* view;
    ProtoPirateDualReceiverCallback callback;
    void* context;
};

typedef struct {
    char tag[4];
    FuriString* frequency_str;
    FuriString* modulation_str;
    float rssi;
    bool external;
} DualChainStatus;

typedef struct {
    ProtoPirateHistory* history;
    FuriMutex* history_mutex;
    uint8_t list_offset;
    uint8_t history_item;
    DualChainStatus chain[2];
    FuriString* history_stat_str;
    FuriString* draw_scratch;
} ProtoPirateDualReceiverModel;

static size_t protopirate_view_dual_receiver_item_count(ProtoPirateDualReceiverModel* model) {
    return model->history ? protopirate_history_get_item(model->history) : 0U;
}

void protopirate_view_dual_receiver_set_callback(
    ProtoPirateDualReceiver* receiver,
    ProtoPirateDualReceiverCallback callback,
    void* context) {
    furi_check(receiver);
    receiver->callback = callback;
    receiver->context = context;
}

void protopirate_view_dual_receiver_set_history(
    ProtoPirateDualReceiver* receiver,
    ProtoPirateHistory* history) {
    furi_check(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        { model->history = history; },
        true);
}

void protopirate_view_dual_receiver_set_history_mutex(
    ProtoPirateDualReceiver* receiver,
    FuriMutex* mutex) {
    furi_check(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        { model->history_mutex = mutex; },
        false);
}

void protopirate_view_dual_receiver_set_chain_status(
    ProtoPirateDualReceiver* receiver,
    uint8_t slot,
    const char* tag,
    const char* frequency_str,
    const char* modulation_str,
    bool external) {
    furi_check(receiver);
    if(slot > 1) return;
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            snprintf(model->chain[slot].tag, sizeof(model->chain[slot].tag), "%s", tag);
            furi_string_set_str(model->chain[slot].frequency_str, frequency_str);
            furi_string_set_str(model->chain[slot].modulation_str, modulation_str);
            model->chain[slot].external = external;
        },
        true);
}

void protopirate_view_dual_receiver_set_rssi(
    ProtoPirateDualReceiver* receiver,
    uint8_t slot,
    float rssi) {
    furi_check(receiver);
    if(slot > 1) return;
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        { model->chain[slot].rssi = rssi; },
        true);
}

void protopirate_view_dual_receiver_set_history_stat(
    ProtoPirateDualReceiver* receiver,
    const char* history_stat_str) {
    furi_check(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        { furi_string_set_str(model->history_stat_str, history_stat_str); },
        true);
}

static void protopirate_view_dual_receiver_update_offset(ProtoPirateDualReceiver* receiver) {
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            size_t history_item = model->history_item;
            size_t list_offset = model->list_offset;
            size_t item_count = protopirate_view_dual_receiver_item_count(model);

            if(history_item < list_offset) {
                model->list_offset = history_item;
            } else if(history_item >= (list_offset + MENU_ITEMS)) {
                model->list_offset = history_item - (MENU_ITEMS - 1);
            }

            if(item_count < MENU_ITEMS) {
                model->list_offset = 0;
            } else {
                size_t max_offset = item_count - MENU_ITEMS;
                if(model->list_offset > max_offset) {
                    model->list_offset = max_offset;
                }
            }
        },
        true);
}

static void protopirate_view_dual_receiver_draw_frame(Canvas* canvas, uint16_t idx, bool scrollbar) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0 + idx * FRAME_HEIGHT, scrollbar ? 122 : 127, FRAME_HEIGHT);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_dot(canvas, 0, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 1, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, 0, (0 + idx * FRAME_HEIGHT) + 1);

    canvas_draw_dot(canvas, 0, (0 + idx * FRAME_HEIGHT) + 11);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, 0 + idx * FRAME_HEIGHT);
    canvas_draw_dot(canvas, scrollbar ? 121 : 126, (0 + idx * FRAME_HEIGHT) + 11);
}

static void
    protopirate_view_dual_receiver_draw_rssi(Canvas* canvas, float rssi, uint8_t x, uint8_t y) {
    uint8_t segments = 0;
    if(rssi > SUBGHZ_RAW_THRESHOLD_MIN) {
        float v = rssi - SUBGHZ_RAW_THRESHOLD_MIN;
        segments = (uint8_t)(v / 13.0f);
        if(segments > 5) segments = 5;
    }
    for(uint8_t i = 0; i < 5; i++) {
        uint8_t bx = x + i * 3;
        if(i < segments) {
            canvas_draw_box(canvas, bx, y - (i + 1), 2, i + 2);
        } else {
            canvas_draw_dot(canvas, bx, y);
        }
    }
}

static void protopirate_view_dual_receiver_draw_status_row(
    Canvas* canvas,
    DualChainStatus* chain,
    uint8_t baseline_y) {
    char row[28];
    snprintf(
        row,
        sizeof(row),
        "%s %s %s",
        chain->tag,
        furi_string_get_cstr(chain->frequency_str),
        furi_string_get_cstr(chain->modulation_str));
    canvas_draw_str(canvas, 0, baseline_y, row);
    protopirate_view_dual_receiver_draw_rssi(canvas, chain->rssi, 108, baseline_y);
}

static void
    protopirate_view_dual_receiver_draw(Canvas* canvas, ProtoPirateDualReceiverModel* model) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(model->history_mutex) {
        furi_mutex_acquire(model->history_mutex, FuriWaitForever);
    }

    size_t item_count = protopirate_view_dual_receiver_item_count(model);
    bool scrollbar = item_count > MENU_ITEMS;

    if(item_count > 0) {
        size_t shift_position = model->list_offset;
        for(size_t i = 0; i < MIN(item_count, (size_t)MENU_ITEMS); i++) {
            size_t idx = shift_position + i;
            protopirate_history_get_text_item_menu(model->history, model->draw_scratch, idx);
            elements_string_fit_width(
                canvas, model->draw_scratch, scrollbar ? MAX_LEN_PX - 6 : MAX_LEN_PX);

            if(model->history_item == idx) {
                protopirate_view_dual_receiver_draw_frame(canvas, i, scrollbar);
            } else {
                canvas_set_color(canvas, ColorBlack);
            }
            canvas_draw_str(
                canvas, 4, 9 + (i * FRAME_HEIGHT), furi_string_get_cstr(model->draw_scratch));
        }

        if(scrollbar) {
            size_t max_scroll = item_count - MENU_ITEMS;
            size_t scroll_pos = shift_position;
            if(scroll_pos > max_scroll) scroll_pos = max_scroll;
            elements_scrollbar_pos(canvas, 128, 0, LIST_HEIGHT, scroll_pos, max_scroll + 1);
        }

        if(model->history_mutex) {
            furi_mutex_release(model->history_mutex);
        }
    } else {
        if(model->history_mutex) {
            furi_mutex_release(model->history_mutex);
        }

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignTop, "Dual RX");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignTop, "Listening on both radios");
    }

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    canvas_draw_line(canvas, 0, STATUS_TOP_Y, 127, STATUS_TOP_Y);
    canvas_draw_str_aligned(
        canvas,
        127,
        STATUS_TOP_Y + 2,
        AlignRight,
        AlignTop,
        furi_string_get_cstr(model->history_stat_str));

    protopirate_view_dual_receiver_draw_status_row(canvas, &model->chain[0], STATUS_TOP_Y + 10);
    protopirate_view_dual_receiver_draw_status_row(canvas, &model->chain[1], STATUS_TOP_Y + 20);
}

static bool protopirate_view_dual_receiver_input(InputEvent* event, void* context) {
    furi_check(context);
    ProtoPirateDualReceiver* receiver = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeLong ||
       event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            with_view_model(
                receiver->view,
                ProtoPirateDualReceiverModel * model,
                {
                    if(model->history_item > 0) model->history_item--;
                },
                true);
            protopirate_view_dual_receiver_update_offset(receiver);
            consumed = true;
            break;
        case InputKeyDown:
            with_view_model(
                receiver->view,
                ProtoPirateDualReceiverModel * model,
                {
                    size_t item_count = protopirate_view_dual_receiver_item_count(model);
                    if(item_count > 0 && model->history_item < item_count - 1) {
                        model->history_item++;
                    }
                },
                true);
            protopirate_view_dual_receiver_update_offset(receiver);
            consumed = true;
            break;
        case InputKeyLeft:
            if(receiver->callback) {
                receiver->callback(
                    ProtoPirateCustomEventViewDualReceiverConfig, receiver->context);
            }
            consumed = true;
            break;
        case InputKeyRight:
            if(event->type == InputTypeLong) {
                bool do_delete = false;
                with_view_model(
                    receiver->view,
                    ProtoPirateDualReceiverModel * model,
                    { do_delete = protopirate_view_dual_receiver_item_count(model) > 0; },
                    false);
                if(do_delete && receiver->callback) {
                    receiver->callback(
                        ProtoPirateCustomEventViewDualReceiverDeleteItem, receiver->context);
                }
            }
            consumed = true;
            break;
        case InputKeyOk: {
            bool do_ok = false;
            with_view_model(
                receiver->view,
                ProtoPirateDualReceiverModel * model,
                { do_ok = protopirate_view_dual_receiver_item_count(model) > 0; },
                false);
            if(do_ok && receiver->callback) {
                receiver->callback(ProtoPirateCustomEventViewDualReceiverOK, receiver->context);
            }
            consumed = true;
            break;
        }
        case InputKeyBack:
            if(receiver->callback) {
                receiver->callback(ProtoPirateCustomEventViewDualReceiverBack, receiver->context);
            }
            consumed = true;
            break;
        default:
            break;
        }
    }

    return consumed;
}

ProtoPirateDualReceiver* protopirate_view_dual_receiver_alloc(void) {
    ProtoPirateDualReceiver* receiver = malloc(sizeof(ProtoPirateDualReceiver));
    furi_check(receiver);
    receiver->callback = NULL;
    receiver->context = NULL;

    receiver->view = view_alloc();
    view_allocate_model(
        receiver->view, ViewModelTypeLocking, sizeof(ProtoPirateDualReceiverModel));
    view_set_context(receiver->view, receiver);
    view_set_draw_callback(receiver->view, (ViewDrawCallback)protopirate_view_dual_receiver_draw);
    view_set_input_callback(receiver->view, protopirate_view_dual_receiver_input);

    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            model->history = NULL;
            model->history_mutex = NULL;
            model->list_offset = 0;
            model->history_item = 0;
            model->history_stat_str = furi_string_alloc();
            model->draw_scratch = furi_string_alloc();
            for(uint8_t i = 0; i < 2; i++) {
                model->chain[i].frequency_str = furi_string_alloc();
                model->chain[i].modulation_str = furi_string_alloc();
                model->chain[i].rssi = -127.0f;
                model->chain[i].external = false;
                model->chain[i].tag[0] = '\0';
            }
        },
        true);

    return receiver;
}

void protopirate_view_dual_receiver_free(ProtoPirateDualReceiver* receiver) {
    furi_check(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            furi_string_free(model->history_stat_str);
            furi_string_free(model->draw_scratch);
            for(uint8_t i = 0; i < 2; i++) {
                furi_string_free(model->chain[i].frequency_str);
                furi_string_free(model->chain[i].modulation_str);
            }
        },
        false);
    view_free(receiver->view);
    free(receiver);
}

View* protopirate_view_dual_receiver_get_view(ProtoPirateDualReceiver* receiver) {
    furi_check(receiver);
    return receiver->view;
}

void protopirate_view_dual_receiver_reset_menu(ProtoPirateDualReceiver* receiver) {
    furi_check(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            model->history = NULL;
            model->history_item = 0;
            model->list_offset = 0;
        },
        false);
}

void protopirate_view_dual_receiver_sync_menu_from_history(
    ProtoPirateDualReceiver* receiver,
    ProtoPirateHistory* history) {
    furi_check(receiver);
    furi_check(history);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            model->history = history;
            size_t item_count = protopirate_view_dual_receiver_item_count(model);
            if(item_count == 0) {
                model->history_item = 0;
                model->list_offset = 0;
            } else {
                if(model->history_item >= item_count) model->history_item = item_count - 1;
                if(model->list_offset >= item_count) model->list_offset = item_count - 1;
            }
        },
        true);
    protopirate_view_dual_receiver_update_offset(receiver);
}

uint16_t protopirate_view_dual_receiver_get_idx_menu(ProtoPirateDualReceiver* receiver) {
    furi_check(receiver);
    uint16_t idx = 0;
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        { idx = model->history_item; },
        false);
    return idx;
}

void protopirate_view_dual_receiver_set_idx_menu(ProtoPirateDualReceiver* receiver, uint16_t idx) {
    furi_check(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            model->history_item = idx;
            size_t item_count = protopirate_view_dual_receiver_item_count(model);
            if(model->history_item >= item_count) {
                model->history_item = item_count > 0 ? item_count - 1 : 0;
            }
        },
        true);
    protopirate_view_dual_receiver_update_offset(receiver);
}

void protopirate_view_dual_receiver_delete_item(ProtoPirateDualReceiver* receiver, uint16_t idx) {
    furi_check(receiver);
    with_view_model(
        receiver->view,
        ProtoPirateDualReceiverModel * model,
        {
            size_t item_count = protopirate_view_dual_receiver_item_count(model);
            if(idx <= item_count) {
                if(item_count == 0) {
                    model->history = NULL;
                    model->history_item = 0;
                    model->list_offset = 0;
                } else {
                    if(model->history_item > idx || model->history_item >= item_count) {
                        if(model->history_item > 0) model->history_item--;
                    }
                    if(model->history_item >= item_count) model->history_item = item_count - 1;
                    if(model->list_offset >= item_count) model->list_offset = item_count - 1;
                }
            }
        },
        true);
    protopirate_view_dual_receiver_update_offset(receiver);
}

#endif // ENABLE_DUAL_RX_SCENE
