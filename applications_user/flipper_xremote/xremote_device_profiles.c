#include "xremote_device_profiles.h"

#include "infrared/infrared_remote.h"
#include "xremote_device_profile.h"
#include "xremote_subghz.h"

#include <assets_icons.h>
#include <dialogs/dialogs.h>
#include <dolphin/dolphin.h>
#include <gui/elements.h>

#include <stdlib.h>
#include <string.h>

typedef enum {
    XRemoteDeviceMenuOpen,
    XRemoteDeviceMenuImportIr,
    XRemoteDeviceMenuImportRf,
    XRemoteDeviceMenuAddRf,
    XRemoteDeviceMenuManage,
    XRemoteDeviceMenuDelete,
    XRemoteDeviceMenuGuide,
} XRemoteDeviceMenu;

typedef enum {
    XRemoteDeviceEditNone,
    XRemoteDeviceEditProfileName,
    XRemoteDeviceEditRfName,
} XRemoteDeviceEditTarget;

typedef struct {
    XRemoteAppContext* app_ctx;
    Storage* storage;
    DialogsApp* dialogs;
    XRemoteDeviceProfile* profile;
    InfraredRemote* ir_remote;
    TextInput* text_input;
    uint16_t selected;
    uint8_t editor_selected;
    uint8_t edit_rf_index;
    XRemoteDeviceEditTarget edit_target;
    uint8_t variants[XREMOTE_DEVICE_RF_COMMAND_MAX];
    char edit_buffer[XREMOTE_DEVICE_NAME_MAX + 1U];
    char status[40];
} XRemoteDeviceContext;

static uint32_t xremote_device_previous_main(void* context) {
    UNUSED(context);
    return XRemoteViewSubmenu;
}

static uint32_t xremote_device_previous_menu(void* context) {
    UNUSED(context);
    return XRemoteViewDeviceProfiles;
}

static void
    xremote_device_message(XRemoteDeviceContext* context, const char* title, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, title, 64, 4, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 30, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, NULL, "OK", NULL);
    dialog_message_show(context->dialogs, message);
    dialog_message_free(message);
}

static bool xremote_device_confirm(
    XRemoteDeviceContext* context,
    const char* title,
    const char* text,
    const char* action) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, title, 64, 4, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 28, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, action, NULL, "Keep");
    const DialogMessageButton result = dialog_message_show(context->dialogs, message);
    dialog_message_free(message);
    return result == DialogMessageButtonLeft;
}

static bool xremote_device_select_file(
    XRemoteDeviceContext* context,
    FuriString* path,
    const char* base_path,
    const char* extension,
    const Icon* icon) {
    storage_simply_mkdir(context->storage, base_path);
    furi_string_set(path, base_path);
    DialogsFileBrowserOptions browser;
    dialog_file_browser_set_basic_options(&browser, extension, icon);
    browser.base_path = base_path;
    return dialog_file_browser_show(context->dialogs, path, path, &browser);
}

static void xremote_device_basename(const char* path, char* output, size_t output_size) {
    if(output_size == 0U) return;
    const char* name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char* extension = strrchr(name, '.');
    const size_t length = extension && extension > name ? (size_t)(extension - name) :
                                                          strlen(name);
    snprintf(output, output_size, "%.*s", (int)length, name);
}

static bool
    xremote_device_profile_select(XRemoteDeviceContext* context, XRemoteDeviceProfile* profile) {
    FuriString* path = furi_string_alloc();
    const bool selected = xremote_device_select_file(
        context,
        path,
        XREMOTE_DEVICE_PROFILE_FOLDER,
        XREMOTE_DEVICE_PROFILE_EXTENSION,
        &I_unknown_10px);
    const bool loaded = selected && xremote_device_profile_load(
                                        context->storage, furi_string_get_cstr(path), profile);
    furi_string_free(path);
    if(selected && !loaded) {
        xremote_device_message(context, "Device Profile", "Profile is invalid\nor unsupported.");
    }
    return loaded;
}

static bool xremote_device_profile_reload(XRemoteDeviceContext* context) {
    XRemoteDeviceProfile* restored = xremote_device_profile_alloc();
    if(!restored) return false;
    const bool loaded =
        xremote_device_profile_load(context->storage, context->profile->path, restored);
    if(loaded) *context->profile = *restored;
    xremote_device_profile_free(restored);
    return loaded;
}

static bool xremote_device_profile_commit(
    XRemoteDeviceContext* context,
    const char* success_text,
    const char* failure_text) {
    if(xremote_device_profile_store(context->storage, context->profile)) {
        snprintf(context->status, sizeof(context->status), "%s", success_text);
        return true;
    }
    const bool restored = xremote_device_profile_reload(context);
    snprintf(
        context->status,
        sizeof(context->status),
        "%s",
        restored ? failure_text : "Save failed; reopen profile");
    return false;
}

static uint16_t xremote_device_ir_count(const XRemoteDeviceContext* context) {
    return context->ir_remote ? infrared_remote_get_button_count(context->ir_remote) : 0U;
}

static uint16_t xremote_device_command_count(const XRemoteDeviceContext* context) {
    return xremote_device_ir_count(context) + context->profile->rf_count;
}

static bool xremote_device_selected_is_rf(const XRemoteDeviceContext* context, uint8_t* rf_index) {
    const uint16_t ir_count = xremote_device_ir_count(context);
    if(context->selected < ir_count) return false;
    const uint16_t index = context->selected - ir_count;
    if(index >= context->profile->rf_count) return false;
    if(rf_index) *rf_index = (uint8_t)index;
    return true;
}

static const char*
    xremote_device_command_name(const XRemoteDeviceContext* context, uint16_t index) {
    const uint16_t ir_count = xremote_device_ir_count(context);
    if(index < ir_count) {
        InfraredRemoteButton* button = infrared_remote_get_button(context->ir_remote, index);
        return button ? infrared_remote_button_get_name(button) : "Missing IR";
    }
    index -= ir_count;
    return index < context->profile->rf_count ? context->profile->rf[index].name : "No command";
}

static void xremote_device_draw_fitted(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    uint8_t max_width,
    Align align,
    const char* text) {
    char fitted[40];
    snprintf(fitted, sizeof(fitted), "%s", text ? text : "");
    size_t length = strlen(fitted);
    while(length > 1U && canvas_string_width(canvas, fitted) > max_width) {
        fitted[--length] = '\0';
    }
    if(text && strlen(text) > length && length > 1U) {
        fitted[length - 1U] = '~';
    }
    canvas_draw_str_aligned(canvas, x, y, align, AlignBottom, fitted);
}

static void xremote_device_context_unload(XRemoteDeviceContext* context) {
    if(context->ir_remote) {
        infrared_remote_free(context->ir_remote);
        context->ir_remote = NULL;
    }
    xremote_device_profile_reset(context->profile);
    memset(context->variants, 0, sizeof(context->variants));
    context->selected = 0U;
    context->status[0] = '\0';
}

static bool xremote_device_context_load(
    XRemoteDeviceContext* context,
    const XRemoteDeviceProfile* profile) {
    xremote_device_context_unload(context);
    *context->profile = *profile;

    if(profile->ir_path[0] != '\0') {
        FuriString* ir_path = furi_string_alloc_set_str(profile->ir_path);
        context->ir_remote = infrared_remote_alloc();
        if(!context->ir_remote || !infrared_remote_load(context->ir_remote, ir_path)) {
            if(context->ir_remote) infrared_remote_free(context->ir_remote);
            context->ir_remote = NULL;
            snprintf(context->status, sizeof(context->status), "IR file missing");
        }
        furi_string_free(ir_path);
    }

    for(uint8_t index = 0U; index < profile->rf_count; index++) {
        XRemoteSubGhzInfo info;
        if(xremote_subghz_inspect(context->storage, profile->rf[index].path, &info) ==
           XRemoteSubGhzStatusOk) {
            context->variants[index] = info.original_variant;
        }
    }
    if(xremote_device_command_count(context) == 0U) {
        snprintf(context->status, sizeof(context->status), "No usable commands");
        return false;
    }
    return true;
}

static void xremote_device_runtime_draw(Canvas* canvas, void* model_context) {
    XRemoteViewModel* model = model_context;
    XRemoteDeviceContext* context = model->context;
    const ViewOrientation orientation = context->app_ctx->app_settings->orientation;
    const uint16_t count = xremote_device_command_count(context);
    const char* command = xremote_device_command_name(context, context->selected);
    uint8_t rf_index = 0U;
    const bool is_rf = xremote_device_selected_is_rf(context, &rf_index);
    const char* source = is_rf ? "RF" : "IR";
    const char* radio = context->profile->radio == XRemoteDeviceRadioExternal ? "EXT" : "INT";

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(orientation == ViewOrientationHorizontal) {
        xremote_device_draw_fitted(canvas, 2, 9, 91, AlignLeft, context->profile->name);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, is_rf ? radio : "IR");
        canvas_draw_line(canvas, 2, 12, 126, 12);

        elements_slightly_rounded_box(canvas, 3, 16, 122, 17);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        xremote_device_draw_fitted(canvas, 64, 29, 112, AlignCenter, command);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontSecondary);

        char details[42];
        if(is_rf) {
            const XRemoteDeviceRfCommand* rf = &context->profile->rf[rf_index];
            snprintf(
                details,
                sizeof(details),
                "%s  %.18s  %s",
                source,
                rf->protocol,
                rf->adapter == XRemoteDeviceAdapterPrinceton4 ?
                    xremote_subghz_variant_name(context->variants[rf_index]) :
                    "Replay");
        } else {
            snprintf(
                details,
                sizeof(details),
                "%s command %u/%u",
                source,
                (unsigned int)(context->selected + 1U),
                (unsigned int)count);
        }
        xremote_device_draw_fitted(canvas, 3, 43, 122, AlignLeft, details);
        if(context->status[0] != '\0') {
            xremote_device_draw_fitted(canvas, 3, 52, 122, AlignLeft, context->status);
        }

        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Send");
        if(is_rf) elements_button_right(canvas, radio);
    } else {
        canvas_draw_str(canvas, 2, 10, "Device");
        canvas_set_font(canvas, FontSecondary);
        xremote_device_draw_fitted(canvas, 2, 20, 60, AlignLeft, context->profile->name);
        canvas_draw_line(canvas, 2, 24, 62, 24);
        elements_slightly_rounded_box(canvas, 2, 29, 60, 30);
        canvas_set_color(canvas, ColorWhite);
        xremote_device_draw_fitted(canvas, 32, 47, 54, AlignCenter, command);
        canvas_set_color(canvas, ColorBlack);
        char details[24];
        snprintf(
            details,
            sizeof(details),
            "%s %u/%u %s",
            source,
            (unsigned int)(context->selected + 1U),
            (unsigned int)count,
            is_rf ? radio : "");
        canvas_draw_str(canvas, 2, 72, details);
        if(is_rf && context->profile->rf[rf_index].adapter == XRemoteDeviceAdapterPrinceton4) {
            canvas_draw_str(
                canvas, 2, 84, xremote_subghz_variant_name(context->variants[rf_index]));
        }
        if(context->status[0] != '\0') {
            elements_multiline_text_aligned(canvas, 2, 92, AlignLeft, AlignTop, context->status);
        }
        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Send");
        if(is_rf) elements_button_right(canvas, radio);
    }
}

static void xremote_device_runtime_send(XRemoteDeviceContext* context) {
    uint8_t rf_index = 0U;
    if(!xremote_device_selected_is_rf(context, &rf_index)) {
        InfraredRemoteButton* button =
            context->ir_remote ?
                infrared_remote_get_button(context->ir_remote, context->selected) :
                NULL;
        InfraredSignal* signal = button ? infrared_remote_button_get_signal(button) : NULL;
        if(signal && xremote_app_send_signal(context->app_ctx, signal)) {
            dolphin_deed(DolphinDeedIrSend);
            snprintf(context->status, sizeof(context->status), "IR sent");
        } else {
            snprintf(context->status, sizeof(context->status), "IR command missing");
        }
        return;
    }

    const XRemoteDeviceRfCommand* command = &context->profile->rf[rf_index];
    const XRemoteSubGhzStatus status = xremote_subghz_send(
        context->storage,
        command->path,
        context->profile->radio,
        command->adapter,
        context->variants[rf_index]);
    snprintf(context->status, sizeof(context->status), "%s", xremote_subghz_status_name(status));
    if(status == XRemoteSubGhzStatusOk) {
        dolphin_deed(DolphinDeedSubGhzSend);
        xremote_app_context_notify_led(context->app_ctx);
    }
}

static bool xremote_device_runtime_input(InputEvent* event, void* view_context) {
    XRemoteView* view = view_context;
    XRemoteDeviceContext* context = xremote_view_get_context(view);
    if(event->type == InputTypeShort && event->key == InputKeyBack) return false;
    if(event->type == InputTypeShort && event->key == InputKeyLeft) return false;

    const uint16_t count = xremote_device_command_count(context);
    if(count == 0U) return true;

    if(event->type == InputTypeShort && event->key == InputKeyUp) {
        context->selected = context->selected == 0U ? count - 1U : context->selected - 1U;
        context->status[0] = '\0';
    } else if(event->type == InputTypeShort && event->key == InputKeyDown) {
        context->selected = (context->selected + 1U) % count;
        context->status[0] = '\0';
    } else if(event->type == InputTypeShort && event->key == InputKeyOk) {
        xremote_device_runtime_send(context);
    } else if(event->type == InputTypeShort && event->key == InputKeyRight) {
        uint8_t rf_index = 0U;
        if(xremote_device_selected_is_rf(context, &rf_index)) {
            const XRemoteDeviceRadio previous = context->profile->radio;
            context->profile->radio = context->profile->radio == XRemoteDeviceRadioInternal ?
                                          XRemoteDeviceRadioExternal :
                                          XRemoteDeviceRadioInternal;
            if(xremote_device_profile_store(context->storage, context->profile)) {
                snprintf(
                    context->status,
                    sizeof(context->status),
                    "%s selected",
                    xremote_device_radio_name(context->profile->radio));
            } else {
                context->profile->radio = previous;
                snprintf(context->status, sizeof(context->status), "Cannot save radio");
            }
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyRight) {
        uint8_t rf_index = 0U;
        if(xremote_device_selected_is_rf(context, &rf_index) &&
           context->profile->rf[rf_index].adapter == XRemoteDeviceAdapterPrinceton4) {
            context->variants[rf_index] = (context->variants[rf_index] + 1U) % 4U;
            snprintf(
                context->status,
                sizeof(context->status),
                "Command %s",
                xremote_subghz_variant_name(context->variants[rf_index]));
        }
    }
    with_view_model(
        xremote_view_get_view(view), XRemoteViewModel * model, { model->context = context; }, true);
    return true;
}

static XRemoteView* xremote_device_runtime_alloc(void* app_ctx, void* model_ctx) {
    XRemoteView* view =
        xremote_view_alloc(app_ctx, xremote_device_runtime_input, xremote_device_runtime_draw);
    xremote_view_set_context(view, model_ctx, NULL);
    xremote_view_model_context_set(view, model_ctx);
    return view;
}

static uint8_t xremote_device_editor_count(const XRemoteDeviceContext* context) {
    return (uint8_t)(2U + context->profile->rf_count);
}

static bool
    xremote_device_editor_rf_index(const XRemoteDeviceContext* context, uint8_t* rf_index) {
    if(context->editor_selected < 2U) return false;
    const uint8_t index = context->editor_selected - 2U;
    if(index >= context->profile->rf_count) return false;
    if(rf_index) *rf_index = index;
    return true;
}

static const char* xremote_device_editor_label(const XRemoteDeviceContext* context) {
    if(context->editor_selected == 0U) return "Profile";
    if(context->editor_selected == 1U) return "IR link";
    return "Sub-GHz command";
}

static void xremote_device_editor_value(
    const XRemoteDeviceContext* context,
    char* output,
    size_t output_size) {
    if(context->editor_selected == 0U) {
        snprintf(output, output_size, "%s", context->profile->name);
        return;
    }
    if(context->editor_selected == 1U) {
        if(context->profile->ir_path[0] == '\0') {
            snprintf(output, output_size, "Not linked");
        } else {
            xremote_device_basename(context->profile->ir_path, output, output_size);
        }
        return;
    }
    uint8_t rf_index = 0U;
    snprintf(
        output,
        output_size,
        "%s",
        xremote_device_editor_rf_index(context, &rf_index) ? context->profile->rf[rf_index].name :
                                                             "No command");
}

static void xremote_device_editor_detail(
    const XRemoteDeviceContext* context,
    char* output,
    size_t output_size) {
    if(context->editor_selected == 0U) {
        const char* ir_state = context->profile->ir_path[0] == '\0' ? "IR off" :
                               storage_file_exists(context->storage, context->profile->ir_path) ?
                                                                      "IR linked" :
                                                                      "IR missing";
        snprintf(
            output, output_size, "%s  RF %u", ir_state, (unsigned int)context->profile->rf_count);
        return;
    }

    if(context->editor_selected == 1U) {
        if(context->profile->ir_path[0] == '\0') {
            snprintf(output, output_size, "OK: choose IR remote");
        } else {
            snprintf(
                output,
                output_size,
                "%s  Hold OK: detach",
                storage_file_exists(context->storage, context->profile->ir_path) ?
                    "Source ready" :
                    "Source missing");
        }
        return;
    }

    uint8_t rf_index = 0U;
    if(!xremote_device_editor_rf_index(context, &rf_index)) {
        snprintf(output, output_size, "No command");
        return;
    }
    const XRemoteDeviceRfCommand* command = &context->profile->rf[rf_index];
    snprintf(
        output,
        output_size,
        "%s  %s",
        storage_file_exists(context->storage, command->path) ? "Source ready" : "Source missing",
        command->protocol);
}

static void xremote_device_editor_draw(Canvas* canvas, void* model_context) {
    XRemoteViewModel* model = model_context;
    XRemoteDeviceContext* context = model->context;
    const ViewOrientation orientation = context->app_ctx->app_settings->orientation;
    uint8_t rf_index = 0U;
    const bool is_rf = xremote_device_editor_rf_index(context, &rf_index);
    char value[40];
    char detail[48];
    xremote_device_editor_value(context, value, sizeof(value));
    xremote_device_editor_detail(context, detail, sizeof(detail));

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(orientation == ViewOrientationHorizontal) {
        canvas_draw_str(canvas, 2, 9, "Profile Editor");
        canvas_set_font(canvas, FontSecondary);
        char position[12];
        snprintf(
            position,
            sizeof(position),
            "%u/%u",
            (unsigned int)(context->editor_selected + 1U),
            (unsigned int)xremote_device_editor_count(context));
        canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, position);
        canvas_draw_line(canvas, 2, 12, 126, 12);

        canvas_draw_str(canvas, 3, 22, xremote_device_editor_label(context));
        elements_slightly_rounded_box(canvas, 3, 25, 122, 16);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        xremote_device_draw_fitted(canvas, 64, 38, 112, AlignCenter, value);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontSecondary);

        xremote_device_draw_fitted(
            canvas, 3, 50, 122, AlignLeft, context->status[0] != '\0' ? context->status : detail);

        if(is_rf) {
            elements_button_left(canvas, rf_index > 0U ? "Up" : "-");
            elements_button_center(canvas, "Rename");
            elements_button_right(
                canvas, rf_index + 1U < context->profile->rf_count ? "Down" : "-");
        } else {
            elements_button_left(canvas, "Back");
            elements_button_center(
                canvas,
                context->editor_selected == 0U       ? "Rename" :
                context->profile->ir_path[0] != '\0' ? "Replace" :
                                                       "Choose");
        }
    } else {
        canvas_draw_str(canvas, 2, 10, "Editor");
        canvas_set_font(canvas, FontSecondary);
        xremote_device_draw_fitted(canvas, 2, 21, 60, AlignLeft, context->profile->name);
        canvas_draw_line(canvas, 2, 24, 62, 24);
        canvas_draw_str(canvas, 2, 36, xremote_device_editor_label(context));
        elements_slightly_rounded_box(canvas, 2, 40, 60, 28);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontPrimary);
        xremote_device_draw_fitted(canvas, 32, 58, 54, AlignCenter, value);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontSecondary);

        xremote_device_draw_fitted(
            canvas, 2, 81, 60, AlignLeft, context->status[0] != '\0' ? context->status : detail);
        if(strcmp(context->status, "Save failed; restored") == 0) {
            canvas_draw_str(canvas, 2, 93, "Saved profile is unchanged");
        } else if(strcmp(context->status, "Save failed; reopen profile") == 0) {
            canvas_draw_str(canvas, 2, 93, "Exit and reopen profile");
        } else if(is_rf) {
            canvas_draw_str(canvas, 2, 93, "Hold OK: remove link");
        }
        if(is_rf) {
            elements_button_left(canvas, rf_index > 0U ? "Up" : "-");
            elements_button_center(canvas, "Edit");
            elements_button_right(canvas, rf_index + 1U < context->profile->rf_count ? "Dn" : "-");
        } else {
            elements_button_left(canvas, "Back");
            elements_button_center(canvas, "Edit");
        }
    }
}

static void xremote_device_editor_refresh(XRemoteDeviceContext* context) {
    const uint8_t count = xremote_device_editor_count(context);
    if(context->editor_selected >= count) {
        context->editor_selected = count > 0U ? count - 1U : 0U;
    }
    view_dispatcher_switch_to_view(context->app_ctx->view_dispatcher, XRemoteViewDeviceEditor);
}

static uint32_t xremote_device_text_input_previous(void* raw_text_input) {
    TextInput* text_input = raw_text_input;
    XRemoteDeviceContext* context = text_input_get_validator_callback_context(text_input);
    context->edit_target = XRemoteDeviceEditNone;
    return XRemoteViewDeviceEditor;
}

static void xremote_device_text_input_done(void* raw_context) {
    XRemoteDeviceContext* context = raw_context;
    bool changed = false;

    if(context->edit_target == XRemoteDeviceEditProfileName) {
        changed = xremote_device_profile_rename(context->profile, context->edit_buffer);
    } else if(
        context->edit_target == XRemoteDeviceEditRfName &&
        context->edit_rf_index < context->profile->rf_count) {
        changed = xremote_device_profile_rename_rf(
            context->profile, context->edit_rf_index, context->edit_buffer);
    }

    if(changed) {
        xremote_device_profile_commit(context, "Name saved", "Save failed; restored");
    } else {
        snprintf(context->status, sizeof(context->status), "Name required");
    }
    context->edit_target = XRemoteDeviceEditNone;
    xremote_device_editor_refresh(context);
}

static void xremote_device_editor_start_text(
    XRemoteDeviceContext* context,
    XRemoteDeviceEditTarget target,
    uint8_t rf_index,
    const char* value,
    const char* title) {
    context->edit_target = target;
    context->edit_rf_index = rf_index;
    snprintf(context->edit_buffer, sizeof(context->edit_buffer), "%s", value);
    text_input_reset(context->text_input);
    text_input_set_header_text(context->text_input, title);
    text_input_set_result_callback(
        context->text_input,
        xremote_device_text_input_done,
        context,
        context->edit_buffer,
        sizeof(context->edit_buffer),
        true);
    view_dispatcher_switch_to_view(context->app_ctx->view_dispatcher, XRemoteViewTextInput);
}

static bool
    xremote_device_editor_select_ir(XRemoteDeviceContext* context, FuriString* selected_path) {
    if(!xremote_device_select_file(
           context, selected_path, XREMOTE_APP_FOLDER, XREMOTE_APP_EXTENSION, &I_ir_10px)) {
        return false;
    }

    InfraredRemote* remote = infrared_remote_alloc();
    const bool valid = remote && infrared_remote_load(remote, selected_path) &&
                       infrared_remote_get_button_count(remote) > 0U;
    if(remote) infrared_remote_free(remote);
    if(!valid) {
        xremote_device_message(context, "IR link", "IR remote has no\nusable commands.");
        return false;
    }
    return true;
}

static void xremote_device_editor_replace_ir(XRemoteDeviceContext* context) {
    FuriString* path = furi_string_alloc();
    if(xremote_device_editor_select_ir(context, path)) {
        xremote_device_profile_set_ir(context->profile, furi_string_get_cstr(path));
        xremote_device_profile_commit(context, "IR link saved", "Save failed; restored");
    }
    furi_string_free(path);
}

static void xremote_device_editor_detach_ir(XRemoteDeviceContext* context) {
    if(context->profile->ir_path[0] == '\0') {
        snprintf(context->status, sizeof(context->status), "No IR link");
        return;
    }
    if(context->profile->rf_count == 0U) {
        snprintf(context->status, sizeof(context->status), "Profile needs command");
        return;
    }
    if(!xremote_device_confirm(context, "Detach IR?", "Source .ir stays on SD.", "Detach")) {
        return;
    }
    if(xremote_device_profile_set_ir(context->profile, "")) {
        xremote_device_profile_commit(context, "IR detached", "Save failed; restored");
    }
}

static void xremote_device_editor_remove_rf(XRemoteDeviceContext* context, uint8_t rf_index) {
    if(rf_index >= context->profile->rf_count) return;
    if(context->profile->ir_path[0] == '\0' && context->profile->rf_count == 1U) {
        snprintf(context->status, sizeof(context->status), "Profile needs command");
        return;
    }

    char prompt[64];
    snprintf(
        prompt,
        sizeof(prompt),
        "Remove %.28s?\n.sub stays on SD.",
        context->profile->rf[rf_index].name);
    if(!xremote_device_confirm(context, "Remove command?", prompt, "Remove")) return;
    if(xremote_device_profile_remove_rf(context->profile, rf_index)) {
        xremote_device_profile_commit(context, "Command removed", "Save failed; restored");
    }
}

static bool xremote_device_editor_input(InputEvent* event, void* view_context) {
    XRemoteView* view = view_context;
    XRemoteDeviceContext* context = xremote_view_get_context(view);
    if(event->type == InputTypeShort && event->key == InputKeyBack) return false;

    const uint8_t count = xremote_device_editor_count(context);
    uint8_t rf_index = 0U;
    const bool is_rf = xremote_device_editor_rf_index(context, &rf_index);

    if(event->type == InputTypeShort && event->key == InputKeyUp) {
        context->editor_selected = context->editor_selected == 0U ? count - 1U :
                                                                    context->editor_selected - 1U;
        context->status[0] = '\0';
    } else if(event->type == InputTypeShort && event->key == InputKeyDown) {
        context->editor_selected = (context->editor_selected + 1U) % count;
        context->status[0] = '\0';
    } else if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(context->editor_selected == 0U) {
            xremote_device_editor_start_text(
                context, XRemoteDeviceEditProfileName, 0U, context->profile->name, "Profile name");
            return true;
        }
        if(context->editor_selected == 1U) {
            xremote_device_editor_replace_ir(context);
        } else if(is_rf) {
            xremote_device_editor_start_text(
                context,
                XRemoteDeviceEditRfName,
                rf_index,
                context->profile->rf[rf_index].name,
                "Command name");
            return true;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        if(context->editor_selected == 1U) {
            xremote_device_editor_detach_ir(context);
        } else if(is_rf) {
            xremote_device_editor_remove_rf(context, rf_index);
        }
    } else if(event->type == InputTypeShort && event->key == InputKeyLeft) {
        if(!is_rf) return false;
        if(rf_index > 0U &&
           xremote_device_profile_move_rf(context->profile, rf_index, rf_index - 1U)) {
            if(xremote_device_profile_commit(context, "Command moved", "Save failed; restored")) {
                context->editor_selected--;
            }
        }
    } else if(event->type == InputTypeShort && event->key == InputKeyRight) {
        if(is_rf && rf_index + 1U < context->profile->rf_count &&
           xremote_device_profile_move_rf(context->profile, rf_index, rf_index + 1U)) {
            if(xremote_device_profile_commit(context, "Command moved", "Save failed; restored")) {
                context->editor_selected++;
            }
        }
    }

    xremote_device_editor_refresh(context);
    with_view_model(
        xremote_view_get_view(view), XRemoteViewModel * model, { model->context = context; }, true);
    return true;
}

static XRemoteView* xremote_device_editor_alloc(void* app_ctx, void* model_ctx) {
    XRemoteView* view =
        xremote_view_alloc(app_ctx, xremote_device_editor_input, xremote_device_editor_draw);
    xremote_view_set_context(view, model_ctx, NULL);
    xremote_view_model_context_set(view, model_ctx);
    return view;
}

static void xremote_device_manage(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    XRemoteDeviceProfile* selected = xremote_device_profile_alloc();
    if(!selected) {
        xremote_device_message(context, "Profile Editor", "Out of memory.");
        return;
    }
    if(xremote_device_profile_select(context, selected)) {
        xremote_device_context_unload(context);
        *context->profile = *selected;
        context->editor_selected = 0U;
        context->status[0] = '\0';
        xremote_app_view_alloc2(
            app, XRemoteViewDeviceEditor, xremote_device_editor_alloc, context);
        xremote_app_view_set_previous_callback(app, xremote_device_previous_menu);
        xremote_app_switch_to_view(app, XRemoteViewDeviceEditor);
    }
    xremote_device_profile_free(selected);
}

static void xremote_device_open(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    XRemoteDeviceProfile* selected = xremote_device_profile_alloc();
    if(!selected) {
        xremote_device_message(context, "Device Profile", "Out of memory.");
        return;
    }
    if(xremote_device_profile_select(context, selected) &&
       xremote_device_context_load(context, selected)) {
        xremote_app_view_free(app);
        xremote_app_view_alloc2(
            app, XRemoteViewDeviceRuntime, xremote_device_runtime_alloc, context);
        xremote_app_view_set_previous_callback(app, xremote_device_previous_menu);
        xremote_app_switch_to_view(app, XRemoteViewDeviceRuntime);
    } else if(selected->path[0] != '\0') {
        xremote_device_message(context, "Device Profile", "No usable commands.");
    }
    xremote_device_profile_free(selected);
}

static void xremote_device_import_ir(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    FuriString* path = furi_string_alloc();
    if(!xremote_device_select_file(
           context, path, XREMOTE_APP_FOLDER, XREMOTE_APP_EXTENSION, &I_ir_10px)) {
        furi_string_free(path);
        return;
    }

    InfraredRemote* remote = infrared_remote_alloc();
    if(!remote || !infrared_remote_load(remote, path) ||
       infrared_remote_get_button_count(remote) == 0U) {
        if(remote) infrared_remote_free(remote);
        xremote_device_message(context, "Import IR", "IR remote has no\nusable commands.");
        furi_string_free(path);
        return;
    }

    char name[XREMOTE_DEVICE_NAME_MAX + 1U];
    xremote_device_basename(furi_string_get_cstr(path), name, sizeof(name));
    XRemoteDeviceProfile* profile = xremote_device_profile_alloc();
    if(!profile) {
        infrared_remote_free(remote);
        furi_string_free(path);
        xremote_device_message(context, "Import IR", "Out of memory.");
        return;
    }
    snprintf(profile->name, sizeof(profile->name), "%s", name);
    snprintf(profile->ir_path, sizeof(profile->ir_path), "%s", furi_string_get_cstr(path));
    const bool path_ready = xremote_device_profile_create_path(
        context->storage, name, profile->path, sizeof(profile->path));
    const bool stored = path_ready && xremote_device_profile_store(context->storage, profile);
    xremote_device_message(
        context,
        "Import IR",
        stored ? "Device profile created.\nUse Add Sub-GHz to mix." : "Cannot create profile.");
    xremote_device_profile_free(profile);
    infrared_remote_free(remote);
    furi_string_free(path);
}

static bool
    xremote_device_add_selected_rf(XRemoteDeviceContext* context, XRemoteDeviceProfile* profile) {
    if(profile->rf_count >= XREMOTE_DEVICE_RF_COMMAND_MAX) {
        xremote_device_message(context, "Add Sub-GHz", "Profile already has\n8 RF commands.");
        return false;
    }

    FuriString* path = furi_string_alloc();
    if(!xremote_device_select_file(context, path, EXT_PATH("subghz"), ".sub", &I_sub1_10px)) {
        furi_string_free(path);
        return false;
    }
    XRemoteSubGhzInfo info;
    const XRemoteSubGhzStatus status =
        xremote_subghz_inspect(context->storage, furi_string_get_cstr(path), &info);
    if(status != XRemoteSubGhzStatusOk) {
        xremote_device_message(context, "Import Sub-GHz", xremote_subghz_status_name(status));
        furi_string_free(path);
        return false;
    }
    if(info.changing_code) {
        xremote_device_message(
            context, "Import Sub-GHz", "Changing-code signal\nis not supported.");
        furi_string_free(path);
        return false;
    }

    char name[XREMOTE_DEVICE_NAME_MAX + 1U];
    xremote_device_basename(furi_string_get_cstr(path), name, sizeof(name));
    const bool added = xremote_device_profile_add_rf(
        profile, name, furi_string_get_cstr(path), info.protocol, info.adapter);
    furi_string_free(path);
    return added;
}

static void xremote_device_import_rf(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    XRemoteDeviceProfile* profile = xremote_device_profile_alloc();
    if(!profile) return;
    if(!xremote_device_add_selected_rf(context, profile)) {
        xremote_device_profile_free(profile);
        return;
    }
    snprintf(profile->name, sizeof(profile->name), "%s", profile->rf[0].name);
    const bool path_ready = xremote_device_profile_create_path(
        context->storage, profile->name, profile->path, sizeof(profile->path));
    const bool stored = path_ready && xremote_device_profile_store(context->storage, profile);
    xremote_device_message(
        context,
        "Import Sub-GHz",
        stored ? "RF device profile created." : "Cannot create profile.");
    xremote_device_profile_free(profile);
}

static void xremote_device_add_rf(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    XRemoteDeviceProfile* profile = xremote_device_profile_alloc();
    if(!profile) return;
    if(xremote_device_profile_select(context, profile) &&
       xremote_device_add_selected_rf(context, profile)) {
        const bool stored = xremote_device_profile_store(context->storage, profile);
        xremote_device_message(
            context,
            "Add Sub-GHz",
            stored ? "Command added to profile." : "Cannot update profile.");
    }
    xremote_device_profile_free(profile);
}

static void xremote_device_delete(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    XRemoteDeviceProfile* profile = xremote_device_profile_alloc();
    if(!profile) return;
    if(xremote_device_profile_select(context, profile)) {
        char prompt[64];
        snprintf(prompt, sizeof(prompt), "Delete %.28s?", profile->name);
        if(xremote_device_confirm(context, "Delete profile?", prompt, "Delete")) {
            xremote_device_message(
                context,
                "Delete profile",
                xremote_device_profile_delete(context->storage, profile) ? "Profile deleted." :
                                                                           "Delete failed.");
        }
    }
    xremote_device_profile_free(profile);
}

static void xremote_device_guide(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    xremote_device_message(
        context,
        "Device Profiles",
        "Import IR or RF, then mix.\n"
        "Runtime: Up/Down select,\n"
        "OK sends, Right INT/EXT.\n"
        "Manage: OK edits, Left/Right\n"
        "moves RF, hold OK unlinks.");
}

static void xremote_device_menu_callback(void* callback_context, uint32_t index) {
    XRemoteApp* app = callback_context;
    switch(index) {
    case XRemoteDeviceMenuOpen:
        xremote_device_open(app);
        break;
    case XRemoteDeviceMenuImportIr:
        xremote_device_import_ir(app);
        break;
    case XRemoteDeviceMenuImportRf:
        xremote_device_import_rf(app);
        break;
    case XRemoteDeviceMenuAddRf:
        xremote_device_add_rf(app);
        break;
    case XRemoteDeviceMenuManage:
        xremote_device_manage(app);
        break;
    case XRemoteDeviceMenuDelete:
        xremote_device_delete(app);
        break;
    case XRemoteDeviceMenuGuide:
        xremote_device_guide(app);
        break;
    default:
        break;
    }
}

static void xremote_device_context_free(void* raw_context) {
    XRemoteDeviceContext* context = raw_context;
    if(!context) return;
    xremote_device_context_unload(context);
    view_dispatcher_remove_view(context->app_ctx->view_dispatcher, XRemoteViewTextInput);
    text_input_free(context->text_input);
    xremote_device_profile_free(context->profile);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(context);
}

XRemoteApp* xremote_device_profiles_alloc(XRemoteAppContext* app_ctx) {
    XRemoteDeviceContext* context = calloc(1U, sizeof(*context));
    if(!context) return NULL;
    context->app_ctx = app_ctx;
    context->storage = furi_record_open(RECORD_STORAGE);
    context->dialogs = furi_record_open(RECORD_DIALOGS);
    context->profile = xremote_device_profile_alloc();
    context->text_input = text_input_alloc();
    if(!context->profile || !context->text_input) {
        if(context->text_input) text_input_free(context->text_input);
        xremote_device_profile_free(context->profile);
        furi_record_close(RECORD_DIALOGS);
        furi_record_close(RECORD_STORAGE);
        free(context);
        return NULL;
    }
    text_input_set_validator(context->text_input, NULL, context);
    View* text_view = text_input_get_view(context->text_input);
    view_set_previous_callback(text_view, xremote_device_text_input_previous);
    view_dispatcher_add_view(app_ctx->view_dispatcher, XRemoteViewTextInput, text_view);
    storage_simply_mkdir(context->storage, XREMOTE_DEVICE_PROFILE_FOLDER);

    XRemoteApp* app = xremote_app_alloc(app_ctx);
    xremote_app_set_user_context(app, context, xremote_device_context_free);
    xremote_app_submenu_alloc(app, XRemoteViewDeviceProfiles, xremote_device_previous_main);
    xremote_app_submenu_add(
        app, "Open Profile", XRemoteDeviceMenuOpen, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "Import IR Remote", XRemoteDeviceMenuImportIr, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "New RF Profile", XRemoteDeviceMenuImportRf, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "Add Sub-GHz", XRemoteDeviceMenuAddRf, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "Manage Profile", XRemoteDeviceMenuManage, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "Delete Profile", XRemoteDeviceMenuDelete, xremote_device_menu_callback);
    xremote_app_submenu_add(app, "Guide", XRemoteDeviceMenuGuide, xremote_device_menu_callback);
    return app;
}
