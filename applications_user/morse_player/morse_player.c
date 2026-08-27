#include <furi.h>
#include <furi_hal.h>

#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>

#include <stdio.h>
#include <string.h>

#include "morse_codec.h"

#define MORSE_PLAYER_TICK_PERIOD_MS 100U
#define MORSE_PLAYER_DEFAULT_WPM 15U
#define MORSE_PLAYER_DEFAULT_TONE_HZ 700U
#define MORSE_PLAYER_DEFAULT_VOLUME 0.70f
#define MORSE_PLAYER_WORKER_STOP_FLAG (1U << 0)

typedef enum {
    MorsePlayerViewMain,
    MorsePlayerViewTextInput,
    MorsePlayerViewPreview,
    MorsePlayerViewSettings,
    MorsePlayerViewMessage,
} MorsePlayerView;

typedef enum {
    MorsePlayerMenuEnterText,
    MorsePlayerMenuPlayLast,
    MorsePlayerMenuSettings,
    MorsePlayerMenuAbout,
} MorsePlayerMenu;

typedef enum {
    MorsePlayerPreviewReady,
    MorsePlayerPreviewPlaying,
    MorsePlayerPreviewComplete,
    MorsePlayerPreviewError,
} MorsePlayerPreviewState;

typedef struct {
    MorsePlayerPreviewState state;
    const char* text;
    const char* display;
    uint32_t total_units;
    uint32_t progress_units;
    size_t unknown_count;
    uint32_t wpm;
    uint32_t tone_hz;
} MorsePlayerPreviewModel;

typedef struct MorsePlayerApp MorsePlayerApp;

struct MorsePlayerApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    TextInput* text_input;
    VariableItemList* settings;
    TextBox* message_box;
    View* preview_view;
    FuriString* message;

    char text_buffer[MORSE_PLAYER_TEXT_MAX_BYTES];
    char display_buffer[MORSE_PLAYER_DISPLAY_MAX_BYTES];
    MorsePlayerSegment* segments;
    MorsePlayerProgramInfo program;

    uint32_t wpm;
    uint32_t tone_hz;
    float volume;

    FuriThread* worker;
    bool worker_started;
    volatile bool worker_finished;
    volatile bool speaker_error;
    volatile uint32_t progress_units;
};

static const uint32_t morse_player_wpm_values[] = {5U, 10U, 15U, 20U, 25U, 30U, 35U, 40U};
static const uint32_t morse_player_tone_values[] = {500U, 600U, 700U, 800U, 900U};
static const char* const morse_player_volume_values[] = {"25%", "50%", "70%", "85%", "100%"};
static const float morse_player_volume_levels[] = {0.25f, 0.50f, 0.70f, 0.85f, 1.00f};

static void morse_player_sync_view(MorsePlayerApp* app, MorsePlayerPreviewState state) {
    with_view_model(
        app->preview_view,
        MorsePlayerPreviewModel * model,
        {
            model->state = state;
            model->text = app->text_buffer;
            model->display = app->display_buffer;
            model->total_units = app->program.total_units;
            model->progress_units = app->progress_units;
            model->unknown_count = app->program.unknown_count;
            model->wpm = app->wpm;
            model->tone_hz = app->tone_hz;
        },
        true);
}

static void morse_player_draw_window(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    const char* text,
    size_t max_bytes) {
    char buffer[48];
    furi_check(max_bytes < sizeof(buffer));
    const size_t length = strlen(text);
    size_t copy_length = length < max_bytes ? length : max_bytes;

    memcpy(buffer, text, copy_length);
    while(copy_length > 0U && ((buffer[copy_length - 1U] & 0xC0U) == 0x80U)) {
        copy_length--;
    }
    if(length > max_bytes && copy_length >= 3U) {
        buffer[copy_length - 3U] = '.';
        buffer[copy_length - 2U] = '.';
        buffer[copy_length - 1U] = '.';
    }
    buffer[copy_length] = '\0';
    canvas_draw_str(canvas, x, y, buffer);
}

static void morse_player_draw_centered_window(
    Canvas* canvas,
    uint8_t y,
    const char* text,
    size_t max_bytes) {
    char buffer[48];
    furi_check(max_bytes < sizeof(buffer));
    const size_t length = strlen(text);
    size_t copy_length = length < max_bytes ? length : max_bytes;

    memcpy(buffer, text, copy_length);
    while(copy_length > 0U && ((buffer[copy_length - 1U] & 0xC0U) == 0x80U)) {
        copy_length--;
    }
    if(length > max_bytes && copy_length >= 3U) {
        buffer[copy_length - 3U] = '.';
        buffer[copy_length - 2U] = '.';
        buffer[copy_length - 1U] = '.';
    }
    buffer[copy_length] = '\0';
    canvas_draw_str_aligned(canvas, 64, y, AlignCenter, AlignBottom, buffer);
}

static void morse_player_draw_header(Canvas* canvas, const char* state) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 8, "Morse Player");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, state);
    canvas_draw_line(canvas, 0, 11, 127, 11);
}

static void morse_player_draw_check(Canvas* canvas) {
    canvas_draw_frame(canvas, 4, 21, 18, 18);
    canvas_draw_line(canvas, 8, 30, 12, 34);
    canvas_draw_line(canvas, 12, 34, 19, 26);
}

static void morse_player_draw_error(Canvas* canvas) {
    canvas_draw_frame(canvas, 4, 21, 18, 18);
    canvas_draw_line(canvas, 8, 26, 18, 35);
    canvas_draw_line(canvas, 18, 26, 8, 35);
}

static void morse_player_preview_draw(Canvas* canvas, void* _model) {
    MorsePlayerPreviewModel* model = _model;
    canvas_clear(canvas);

    switch(model->state) {
    case MorsePlayerPreviewReady: {
        char state[16];
        if(model->unknown_count > 0U) {
            snprintf(state, sizeof(state), "SKIP %u", (unsigned)model->unknown_count);
        } else {
            snprintf(state, sizeof(state), "READY");
        }
        morse_player_draw_header(canvas, state);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 21, "TEXT");
        canvas_draw_frame(canvas, 24, 14, 102, 14);
        morse_player_draw_window(canvas, 27, 24, model->text, 16U);
        canvas_draw_str(canvas, 2, 39, "CODE");
        canvas_draw_frame(canvas, 24, 32, 102, 14);
        canvas_set_font(canvas, FontKeyboard);
        morse_player_draw_window(canvas, 27, 42, model->display, 16U);
        elements_button_left(canvas, "Edit");
        elements_button_center(canvas, "Play");
        break;
    }

    case MorsePlayerPreviewPlaying: {
        morse_player_draw_header(canvas, "PLAY");
        canvas_set_font(canvas, FontPrimary);
        morse_player_draw_centered_window(canvas, 27, model->text, 16U);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 35, AlignCenter, AlignBottom, "Playing Morse audio");
        canvas_draw_frame(canvas, 2, 38, 124, 7);
        if(model->total_units > 0U) {
            uint32_t width = (model->progress_units * 120U) / model->total_units;
            if(width > 120U) width = 120U;
            if(width > 0U) canvas_draw_box(canvas, 4, 40, width, 3);
        }
        char status[32];
        snprintf(status, sizeof(status), "%luHz  %luWPM", model->tone_hz, model->wpm);
        canvas_draw_str_aligned(canvas, 126, 51, AlignRight, AlignBottom, status);
        elements_button_center(canvas, "Stop");
        break;
    }

    case MorsePlayerPreviewComplete:
        morse_player_draw_header(canvas, "DONE");
        morse_player_draw_check(canvas);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 29, 30, "Playback complete");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 29, 42, "OK to replay");
        elements_button_left(canvas, "Edit");
        elements_button_center(canvas, "Replay");
        break;

    case MorsePlayerPreviewError:
        morse_player_draw_header(canvas, "ERROR");
        morse_player_draw_error(canvas);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 27, 30, "Speaker unavailable");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 27, 42, "Close other audio apps");
        elements_button_center(canvas, "Back");
        break;
    }
}

static void morse_player_worker_set_progress(MorsePlayerApp* app, uint32_t units) {
    app->progress_units += units;
}

static bool morse_player_worker_wait(uint32_t duration_ms) {
    const uint32_t flags = furi_thread_flags_wait(
        MORSE_PLAYER_WORKER_STOP_FLAG, FuriFlagWaitAny, duration_ms);
    return (flags & MORSE_PLAYER_WORKER_STOP_FLAG) != 0U;
}

static int32_t morse_player_worker(void* context) {
    MorsePlayerApp* app = context;
    bool stopped = false;

    if(!furi_hal_speaker_acquire(1000U)) {
        app->speaker_error = true;
        app->worker_finished = true;
        return 0;
    }

    for(size_t i = 0U; i < app->program.segment_count; i++) {
        if((furi_thread_flags_get() & MORSE_PLAYER_WORKER_STOP_FLAG) != 0U) break;

        const MorsePlayerSegment segment = app->segments[i];
        const uint8_t units = segment & MORSE_PLAYER_SEGMENT_UNITS_MASK;
        const uint32_t duration_ms =
            ((uint32_t)units * 1200U + app->wpm / 2U) / app->wpm;
        const bool tone = (segment & MORSE_PLAYER_TONE_FLAG) != 0U;

        if(tone) {
            furi_hal_speaker_start((float)app->tone_hz, app->volume);
        } else {
            furi_hal_speaker_stop();
        }

        stopped = morse_player_worker_wait(duration_ms);
        furi_hal_speaker_stop();
        if(stopped) break;
        morse_player_worker_set_progress(app, units);
    }

    furi_hal_speaker_stop();
    furi_hal_speaker_release();
    app->worker_finished = true;
    return 0;
}

static void morse_player_join_worker(MorsePlayerApp* app, bool request_stop) {
    if(!app->worker_started) return;

    if(request_stop) {
        furi_thread_flags_set(furi_thread_get_id(app->worker), MORSE_PLAYER_WORKER_STOP_FLAG);
    }
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    app->worker = NULL;
    app->worker_started = false;
}

static void morse_player_stop(MorsePlayerApp* app) {
    morse_player_join_worker(app, true);
    app->worker_finished = false;
    app->speaker_error = false;
    app->progress_units = 0U;
}

static bool morse_player_start(MorsePlayerApp* app) {
    if(app->program.segment_count == 0U) return false;

    if(app->wpm == 0U) app->wpm = MORSE_PLAYER_DEFAULT_WPM;

    morse_player_stop(app);
    app->worker_finished = false;
    app->speaker_error = false;
    app->progress_units = 0U;
    app->worker = furi_thread_alloc_ex("MorsePlayer", 1024U, morse_player_worker, app);
    app->worker_started = true;
    furi_thread_start(app->worker);
    morse_player_sync_view(app, MorsePlayerPreviewPlaying);
    return true;
}

static void morse_player_tick(void* context) {
    MorsePlayerApp* app = context;
    if(!app->worker_started) return;

    if(app->worker_finished) {
        morse_player_join_worker(app, false);
        morse_player_sync_view(
            app, app->speaker_error ? MorsePlayerPreviewError : MorsePlayerPreviewComplete);
    } else {
        morse_player_sync_view(app, MorsePlayerPreviewPlaying);
    }
}

static uint32_t morse_player_nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t morse_player_nav_main(void* context) {
    UNUSED(context);
    return MorsePlayerViewMain;
}

static void morse_player_show_message(MorsePlayerApp* app, const char* message) {
    furi_string_set_str(app->message, message);
    text_box_set_text(app->message_box, furi_string_get_cstr(app->message));
    view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewMessage);
}

static bool morse_player_rebuild_program(MorsePlayerApp* app) {
    MorsePlayerProgramInfo program;
    if(!morse_player_build_program(
           app->text_buffer,
           app->segments,
           MORSE_PLAYER_MAX_SEGMENTS,
           &program) ||
       !morse_player_build_display(
           app->text_buffer,
           app->display_buffer,
           sizeof(app->display_buffer),
           NULL)) {
        app->program = (MorsePlayerProgramInfo){0};
        app->display_buffer[0] = '\0';
        return false;
    }

    app->program = program;
    return true;
}

static void morse_player_text_input_callback(void* context) {
    MorsePlayerApp* app = context;
    if(!morse_player_rebuild_program(app)) {
        morse_player_show_message(
            app, "No supported symbols found.\n\nUse letters, numbers,\nand common punctuation.");
        return;
    }

    morse_player_sync_view(app, MorsePlayerPreviewReady);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewPreview);
}

static void morse_player_show_text_input(MorsePlayerApp* app) {
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Enter text for Morse:");
    text_input_set_result_callback(
        app->text_input,
        morse_player_text_input_callback,
        app,
        app->text_buffer,
        sizeof(app->text_buffer),
        false);
    text_input_set_minimum_length(app->text_input, 1U);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewTextInput);
}

static void morse_player_menu_callback(void* context, uint32_t index) {
    MorsePlayerApp* app = context;

    switch(index) {
    case MorsePlayerMenuEnterText:
        morse_player_show_text_input(app);
        break;
    case MorsePlayerMenuPlayLast:
        if(app->program.segment_count == 0U) {
            morse_player_show_message(app, "Enter text first.");
        } else {
            morse_player_sync_view(app, MorsePlayerPreviewReady);
            view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewPreview);
        }
        break;
    case MorsePlayerMenuSettings:
        view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewSettings);
        break;
    case MorsePlayerMenuAbout:
        morse_player_show_message(
            app,
            "Morse Player 0.1\n\n"
            "Enter text and press OK\n"
            "to hear standard Morse\n"
            "through the Flipper speaker.\n\n"
            "No radio transmission is\n"
            "performed by this app.");
        break;
    default:
        break;
    }
}

static bool morse_player_preview_input(InputEvent* event, void* context) {
    MorsePlayerApp* app = context;

    if(event->key == InputKeyBack) {
        if(event->type == InputTypeShort || event->type == InputTypeLong) {
            morse_player_stop(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewMain);
            return true;
        }
        return false;
    }

    if(event->type != InputTypeShort) return true;

    MorsePlayerPreviewState state;
    with_view_model(
        app->preview_view,
        MorsePlayerPreviewModel * model,
        { state = model->state; },
        false);

    if(event->key == InputKeyOk) {
        if(state == MorsePlayerPreviewPlaying) {
            morse_player_stop(app);
            morse_player_sync_view(app, MorsePlayerPreviewReady);
        } else if(state == MorsePlayerPreviewReady || state == MorsePlayerPreviewComplete) {
            if(!morse_player_start(app)) {
                morse_player_sync_view(app, MorsePlayerPreviewError);
            }
        } else if(state == MorsePlayerPreviewError) {
            view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewMain);
        }
        return true;
    }

    if(event->key == InputKeyLeft && state != MorsePlayerPreviewPlaying) {
        morse_player_show_text_input(app);
        return true;
    }

    return true;
}

static void morse_player_wpm_changed(VariableItem* item) {
    MorsePlayerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->wpm = morse_player_wpm_values[index];
    char value[8];
    snprintf(value, sizeof(value), "%lu", app->wpm);
    variable_item_set_current_value_text(item, value);
}

static void morse_player_tone_changed(VariableItem* item) {
    MorsePlayerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->tone_hz = morse_player_tone_values[index];
    char value[12];
    snprintf(value, sizeof(value), "%lu Hz", app->tone_hz);
    variable_item_set_current_value_text(item, value);
}

static void morse_player_volume_changed(VariableItem* item) {
    MorsePlayerApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->volume = morse_player_volume_levels[index];
    variable_item_set_current_value_text(item, morse_player_volume_values[index]);
}

static uint8_t morse_player_find_u32_index(
    const uint32_t* values,
    size_t count,
    uint32_t value,
    uint8_t fallback) {
    for(size_t i = 0U; i < count; i++) {
        if(values[i] == value) return (uint8_t)i;
    }
    return fallback;
}

static void morse_player_show_settings(MorsePlayerApp* app) {
    variable_item_list_reset(app->settings);

    VariableItem* item = variable_item_list_add(
        app->settings,
        "Speed (WPM)",
        sizeof(morse_player_wpm_values) / sizeof(morse_player_wpm_values[0]),
        morse_player_wpm_changed,
        app);
    const uint8_t wpm_index = morse_player_find_u32_index(
        morse_player_wpm_values,
        sizeof(morse_player_wpm_values) / sizeof(morse_player_wpm_values[0]),
        app->wpm,
        2U);
    char value[12];
    snprintf(value, sizeof(value), "%lu", app->wpm);
    variable_item_set_current_value_index(item, wpm_index);
    variable_item_set_current_value_text(item, value);

    item = variable_item_list_add(
        app->settings,
        "Tone",
        sizeof(morse_player_tone_values) / sizeof(morse_player_tone_values[0]),
        morse_player_tone_changed,
        app);
    const uint8_t tone_index = morse_player_find_u32_index(
        morse_player_tone_values,
        sizeof(morse_player_tone_values) / sizeof(morse_player_tone_values[0]),
        app->tone_hz,
        2U);
    snprintf(value, sizeof(value), "%lu Hz", app->tone_hz);
    variable_item_set_current_value_index(item, tone_index);
    variable_item_set_current_value_text(item, value);

    item = variable_item_list_add(
        app->settings,
        "Volume",
        sizeof(morse_player_volume_values) / sizeof(morse_player_volume_values[0]),
        morse_player_volume_changed,
        app);
    uint8_t volume_index = 2U;
    for(size_t i = 0U; i < sizeof(morse_player_volume_levels) / sizeof(morse_player_volume_levels[0]);
        i++) {
        if(app->volume == morse_player_volume_levels[i]) volume_index = (uint8_t)i;
    }
    variable_item_set_current_value_index(item, volume_index);
    variable_item_set_current_value_text(item, morse_player_volume_values[volume_index]);

    view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewSettings);
}

static MorsePlayerApp* morse_player_alloc(void) {
    MorsePlayerApp* app = malloc(sizeof(MorsePlayerApp));
    memset(app, 0, sizeof(*app));

    app->gui = furi_record_open(RECORD_GUI);
    app->message = furi_string_alloc();
    app->segments = malloc(sizeof(MorsePlayerSegment) * MORSE_PLAYER_MAX_SEGMENTS);
    app->wpm = MORSE_PLAYER_DEFAULT_WPM;
    app->tone_hz = MORSE_PLAYER_DEFAULT_TONE_HZ;
    app->volume = MORSE_PLAYER_DEFAULT_VOLUME;

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, morse_player_tick, MORSE_PLAYER_TICK_PERIOD_MS);

    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "Morse Player");
    submenu_add_item(
        app->main_menu,
        "Enter text",
        MorsePlayerMenuEnterText,
        morse_player_menu_callback,
        app);
    submenu_add_item(
        app->main_menu,
        "Play last",
        MorsePlayerMenuPlayLast,
        morse_player_menu_callback,
        app);
    submenu_add_item(
        app->main_menu,
        "Settings",
        MorsePlayerMenuSettings,
        morse_player_menu_callback,
        app);
    submenu_add_item(
        app->main_menu,
        "About",
        MorsePlayerMenuAbout,
        morse_player_menu_callback,
        app);
    view_set_previous_callback(submenu_get_view(app->main_menu), morse_player_nav_exit);
    view_dispatcher_add_view(
        app->view_dispatcher, MorsePlayerViewMain, submenu_get_view(app->main_menu));

    app->text_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->text_input), morse_player_nav_main);
    view_dispatcher_add_view(
        app->view_dispatcher,
        MorsePlayerViewTextInput,
        text_input_get_view(app->text_input));

    app->preview_view = view_alloc();
    view_allocate_model(
        app->preview_view, ViewModelTypeLocking, sizeof(MorsePlayerPreviewModel));
    view_set_context(app->preview_view, app);
    view_set_draw_callback(app->preview_view, morse_player_preview_draw);
    view_set_input_callback(app->preview_view, morse_player_preview_input);
    view_set_previous_callback(app->preview_view, morse_player_nav_main);
    view_dispatcher_add_view(app->view_dispatcher, MorsePlayerViewPreview, app->preview_view);

    app->settings = variable_item_list_alloc();
    view_set_previous_callback(
        variable_item_list_get_view(app->settings), morse_player_nav_main);
    view_dispatcher_add_view(
        app->view_dispatcher,
        MorsePlayerViewSettings,
        variable_item_list_get_view(app->settings));

    app->message_box = text_box_alloc();
    text_box_set_font(app->message_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->message_box), morse_player_nav_main);
    view_dispatcher_add_view(
        app->view_dispatcher, MorsePlayerViewMessage, text_box_get_view(app->message_box));

    morse_player_sync_view(app, MorsePlayerPreviewReady);
    return app;
}

static void morse_player_free(MorsePlayerApp* app) {
    furi_assert(app);
    morse_player_stop(app);

    view_dispatcher_remove_view(app->view_dispatcher, MorsePlayerViewMessage);
    view_dispatcher_remove_view(app->view_dispatcher, MorsePlayerViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, MorsePlayerViewPreview);
    view_dispatcher_remove_view(app->view_dispatcher, MorsePlayerViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, MorsePlayerViewMain);

    text_box_free(app->message_box);
    variable_item_list_free(app->settings);
    view_free(app->preview_view);
    text_input_free(app->text_input);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    free(app->segments);
    furi_string_free(app->message);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t morse_player_app(void* context) {
    UNUSED(context);
    MorsePlayerApp* app = morse_player_alloc();
    morse_player_show_settings(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MorsePlayerViewMain);
    view_dispatcher_run(app->view_dispatcher);
    morse_player_free(app);
    return 0;
}
