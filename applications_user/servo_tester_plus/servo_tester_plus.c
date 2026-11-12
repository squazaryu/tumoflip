#include "servo_hardware.h"
#include "servo_model.h"

#include <dialogs/dialogs.h>
#include <furi.h>
#include <furi_hal_power.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/modules/number_input.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>

#include <stdio.h>
#include <stdlib.h>

enum {ServoViewMenu, ServoViewOutput, ServoViewCalibration, ServoViewNumber, ServoViewHelp};

typedef struct {
    ServoModel model;
    ServoHardwareStatus status;
    bool gpio_power;
} ServoViewModel;

typedef struct {
    ServoModel model;
    ServoHardware hardware;
    ServoHardwareStatus status;
    bool gpio_power;
    bool edit_maximum;
    Gui* gui;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    View* view;
    Submenu* menu;
    Submenu* calibration;
    NumberInput* number;
    TextBox* help;
} ServoApp;

static void servo_stop(ServoApp* app) {
    servo_model_stop(&app->model);
    servo_hardware_stop(&app->hardware);
}

static uint32_t servo_previous(void* context) {
    ServoApp* app = context;
    servo_stop(app);
    return ServoViewMenu;
}

static uint32_t servo_exit(void* context) {
    servo_stop(context);
    return VIEW_NONE;
}

static uint32_t servo_calibration_previous(void* context) {
    UNUSED(context);
    return ServoViewCalibration;
}

static void servo_refresh(ServoApp* app) {
    with_view_model(
        app->view,
        ServoViewModel * model,
        {
            model->model = app->model;
            model->gpio_power = app->gpio_power;
            model->status = app->status;
        },
        true);
}

static void servo_draw(Canvas* canvas, void* context) {
    const ServoViewModel* view = context;
    const ServoModel* model = &view->model;
    char text[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 1, 8, "Servo Tester+");
    const char* status = model->armed ? "ARMED" :
                         view->status == ServoHardwareBusy ? "BUSY" :
                         view->status == ServoHardwarePowerError ? "5V ERR" : "OFF";
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, status);
    canvas_draw_line(canvas, 0, 10, 127, 10);
    snprintf(text, sizeof(text), "%u", model->pulse_us);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 59, 22, AlignCenter, AlignCenter, text);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 104, 33, "us");
    const char* modes[] = {"Manual", "Center", "Sweep"};
    snprintf(text, sizeof(text), "%s / 50 Hz", modes[model->mode]);
    canvas_draw_str(canvas, 1, 41, text);
    snprintf(text, sizeof(text), "%u..%u us", model->minimum_us, model->maximum_us);
    canvas_draw_str(canvas, 1, 51, text);
    canvas_draw_str_aligned(
        canvas, 127, 51, AlignRight, AlignBottom, view->gpio_power ? "GPIO 5V" : "EXT");
    elements_button_left(canvas, "-10");
    elements_button_center(canvas, model->armed ? "Stop" : "Start");
    elements_button_right(canvas, "+10");
}

static void servo_toggle(ServoApp* app) {
    if(app->model.armed) {
        servo_stop(app);
        return;
    }
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Start servo PWM?", 64, 4, AlignCenter, AlignTop);
    dialog_message_set_text(
        message, "Own fixture only.\nCheck pulse / wiring.\nStop at any stall.",
        64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Cancel", NULL, "Start");
    const bool confirmed = dialog_message_show(app->dialogs, message) == DialogMessageButtonRight;
    dialog_message_free(message);
    if(!confirmed) return;
    app->status = servo_hardware_start(&app->hardware, app->model.pulse_us, app->gpio_power);
    if(app->status == ServoHardwareOk) servo_model_arm(&app->model, furi_get_tick());
}

static bool servo_input(InputEvent* event, void* context) {
    ServoApp* app = context;
    if(event->type != InputTypeShort || event->key == InputKeyBack) return false;
    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        servo_model_nudge(&app->model, event->key == InputKeyRight ? 1 : -1);
        if(app->model.armed) {
            if(!servo_hardware_apply(&app->hardware, app->model.pulse_us)) servo_stop(app);
        } else {
            servo_hardware_stop(&app->hardware);
        }
    } else if(event->key == InputKeyOk) {
        servo_toggle(app);
    } else {
        return false;
    }
    servo_refresh(app);
    return true;
}

static void servo_tick(void* context) {
    ServoApp* app = context;
    if(app->hardware.otg_owned && !furi_hal_power_is_otg_enabled()) {
        servo_stop(app);
        app->status = ServoHardwarePowerError;
    }
    if(servo_model_tick(&app->model, furi_get_tick())) {
        if(app->model.armed) {
            if(!servo_hardware_apply(&app->hardware, app->model.pulse_us)) servo_stop(app);
        } else {
            servo_hardware_stop(&app->hardware);
        }
    }
    servo_refresh(app);
}

static void servo_build_calibration(ServoApp* app);

static void servo_number_done(void* context, int32_t value) {
    ServoApp* app = context;
    servo_model_limits(
        &app->model,
        app->edit_maximum ? app->model.minimum_us : (uint16_t)value,
        app->edit_maximum ? (uint16_t)value : app->model.maximum_us);
    servo_build_calibration(app);
    view_dispatcher_switch_to_view(app->dispatcher, ServoViewCalibration);
}

static void servo_edit_limit(void* context, uint32_t index) {
    ServoApp* app = context;
    servo_stop(app);
    app->edit_maximum = index != 0;
    number_input_set_header_text(app->number, index ? "Maximum pulse us" : "Minimum pulse us");
    number_input_set_result_callback(
        app->number,
        servo_number_done,
        app,
        index ? app->model.maximum_us : app->model.minimum_us,
        index ? app->model.minimum_us + 100 : 500,
        index ? 2500 : app->model.maximum_us - 100);
    view_dispatcher_switch_to_view(app->dispatcher, ServoViewNumber);
}

static void servo_build_calibration(ServoApp* app) {
    char label[32];
    submenu_reset(app->calibration);
    submenu_set_header(app->calibration, "Session limits");
    snprintf(label, sizeof(label), "Minimum: %u us", app->model.minimum_us);
    submenu_add_item(app->calibration, label, 0, servo_edit_limit, app);
    snprintf(label, sizeof(label), "Maximum: %u us", app->model.maximum_us);
    submenu_add_item(app->calibration, label, 1, servo_edit_limit, app);
}

static void servo_build_menu(ServoApp* app);

static void servo_menu_action(void* context, uint32_t index) {
    ServoApp* app = context;
    servo_stop(app);
    app->status = ServoHardwareOk;
    if(index < 3) {
        servo_model_mode(&app->model, (ServoMode)index, furi_get_tick());
        servo_refresh(app);
        view_dispatcher_switch_to_view(app->dispatcher, ServoViewOutput);
    } else if(index == 3) {
        servo_build_calibration(app);
        view_dispatcher_switch_to_view(app->dispatcher, ServoViewCalibration);
    } else if(index == 4) {
        app->gpio_power = !app->gpio_power;
        servo_build_menu(app);
    } else if(index == 5) {
        view_dispatcher_switch_to_view(app->dispatcher, ServoViewHelp);
    }
}

static void servo_build_menu(ServoApp* app) {
    submenu_reset(app->menu);
    submenu_set_header(app->menu, "Servo Tester+");
    const char* labels[] = {"Manual pulse", "Center", "Sweep", "Calibrate limits",
                           app->gpio_power ? "Power: GPIO 5V" : "Power: external", "Wiring / limits"};
    for(unsigned i = 0; i < COUNT_OF(labels); i++) {
        submenu_add_item(app->menu, labels[i], i, servo_menu_action, app);
    }
}

int32_t servo_tester_plus_app(void* context) {
    UNUSED(context);
    ServoApp* app = calloc(1, sizeof(*app));
    if(!app) return -1;
    servo_model_init(&app->model);
    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->dispatcher = view_dispatcher_alloc();
    app->view = view_alloc();
    app->menu = submenu_alloc();
    app->calibration = submenu_alloc();
    app->number = number_input_alloc();
    app->help = text_box_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->dispatcher, servo_tick, 50);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(ServoViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, servo_draw);
    view_set_input_callback(app->view, servo_input);
    view_set_previous_callback(app->view, servo_previous);
    view_set_previous_callback(submenu_get_view(app->menu), servo_exit);
    view_set_previous_callback(submenu_get_view(app->calibration), servo_previous);
    view_set_previous_callback(number_input_get_view(app->number), servo_calibration_previous);
    view_set_previous_callback(text_box_get_view(app->help), servo_previous);
    text_box_set_text(app->help,
        "Servo Tester+\n\nPA7 / pin2: signal\n3.3V logic, shared GND\nExternal power is best.\n\n"
        "Measure PWM first.\nNever force end stops.\nNominal shaft travel\ndepends on your servo.\n\n"
        "GPIO 5V is optional.\nOnly an app-owned rail\nis disabled on exit.\nNo power on at launch.\n\n"
        "Limits are per session.\nBack stops the output.\nRF/actuator hardware\nacceptance pending.");
    view_dispatcher_add_view(app->dispatcher, ServoViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, ServoViewOutput, app->view);
    view_dispatcher_add_view(app->dispatcher, ServoViewCalibration, submenu_get_view(app->calibration));
    view_dispatcher_add_view(app->dispatcher, ServoViewNumber, number_input_get_view(app->number));
    view_dispatcher_add_view(app->dispatcher, ServoViewHelp, text_box_get_view(app->help));
    servo_build_menu(app);
    view_dispatcher_switch_to_view(app->dispatcher, ServoViewMenu);
    view_dispatcher_run(app->dispatcher);
    servo_stop(app);
    for(unsigned i = ServoViewMenu; i <= ServoViewHelp; i++) view_dispatcher_remove_view(app->dispatcher, i);
    view_free(app->view);
    submenu_free(app->menu);
    submenu_free(app->calibration);
    number_input_free(app->number);
    text_box_free(app->help);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
