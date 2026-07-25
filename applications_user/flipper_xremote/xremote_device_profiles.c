#include "xremote_device_profiles.h"

#include "infrared/infrared_remote.h"
#include "xremote_device_bundle.h"
#include "xremote_device_profile.h"
#include "xremote_subghz.h"

#include <assets_icons.h>
#include <dialogs/dialogs.h>
#include <dolphin/dolphin.h>
#include <gui/elements.h>

#include <stdlib.h>
#include <string.h>

#define XREMOTE_DEVICE_PAGE_SIZE            4U
#define XREMOTE_DEVICE_GRID_COLUMNS         2U
#define XREMOTE_DEVICE_LIBRARY_MAX          16U
#define XREMOTE_DEVICE_LIBRARY_FILENAME_MAX 96U
#define XREMOTE_DEVICE_LIBRARY_ROWS         3U

typedef enum {
    XRemoteDeviceMenuLibrary,
    XRemoteDeviceMenuImportIr,
    XRemoteDeviceMenuImportRf,
    XRemoteDeviceMenuAddRf,
    XRemoteDeviceMenuImportBundle,
    XRemoteDeviceMenuGuide,
} XRemoteDeviceMenu;

typedef enum {
    XRemoteDeviceEditNone,
    XRemoteDeviceEditProfileName,
    XRemoteDeviceEditRfName,
} XRemoteDeviceEditTarget;

typedef enum {
    XRemoteDeviceLibraryReady,
    XRemoteDeviceLibraryMissing,
    XRemoteDeviceLibraryInvalid,
} XRemoteDeviceLibraryHealth;

typedef enum {
    XRemoteDeviceLibraryActionOpen,
    XRemoteDeviceLibraryActionEdit,
    XRemoteDeviceLibraryActionRepair,
    XRemoteDeviceLibraryActionDuplicate,
    XRemoteDeviceLibraryActionExport,
    XRemoteDeviceLibraryActionDelete,
    XRemoteDeviceLibraryActionCount,
} XRemoteDeviceLibraryAction;

typedef struct {
    char name[XREMOTE_DEVICE_NAME_MAX + 1U];
    char filename[XREMOTE_DEVICE_LIBRARY_FILENAME_MAX];
    uint16_t ir_count;
    uint8_t rf_count;
    XRemoteDeviceLibraryHealth health;
} XRemoteDeviceLibraryEntry;

typedef struct {
    XRemoteAppContext* app_ctx;
    XRemoteApp* owner_app;
    Storage* storage;
    DialogsApp* dialogs;
    XRemoteDeviceProfile* profile;
    InfraredRemote* ir_remote;
    TextInput* text_input;
    XRemoteView* library_view;
    XRemoteDeviceLibraryEntry library[XREMOTE_DEVICE_LIBRARY_MAX];
    uint8_t library_count;
    uint8_t library_selected;
    uint8_t library_top;
    uint8_t library_action;
    bool library_actions;
    bool library_truncated;
    uint16_t selected;
    uint8_t editor_selected;
    uint8_t edit_rf_index;
    XRemoteDeviceEditTarget edit_target;
    uint8_t variants[XREMOTE_DEVICE_RF_COMMAND_MAX];
    XRemoteSubGhzStatus rf_status[XREMOTE_DEVICE_RF_COMMAND_MAX];
    char edit_buffer[XREMOTE_DEVICE_NAME_MAX + 1U];
    char status[40];
    char library_notice[40];
} XRemoteDeviceContext;

static void xremote_device_library_refresh(XRemoteDeviceContext* context);
static void xremote_device_context_unload(XRemoteDeviceContext* context);

static uint32_t xremote_device_previous_main(void* context) {
    UNUSED(context);
    return XRemoteViewSubmenu;
}

static uint32_t xremote_device_previous_library(void* raw_view) {
    XRemoteView* view = raw_view;
    XRemoteDeviceContext* context = view ? xremote_view_get_context(view) : NULL;
    if(context) {
        xremote_device_context_unload(context);
        xremote_device_library_refresh(context);
    }
    return XRemoteViewDeviceLibrary;
}

static uint32_t xremote_device_library_previous(void* context) {
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

static bool xremote_device_command_is_rf(
    const XRemoteDeviceContext* context,
    uint16_t command_index,
    uint8_t* rf_index) {
    const uint16_t ir_count = xremote_device_ir_count(context);
    if(command_index < ir_count) return false;
    const uint16_t index = command_index - ir_count;
    if(index >= context->profile->rf_count) return false;
    if(rf_index) *rf_index = (uint8_t)index;
    return true;
}

static bool xremote_device_selected_is_rf(const XRemoteDeviceContext* context, uint8_t* rf_index) {
    return xremote_device_command_is_rf(context, context->selected, rf_index);
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

static uint16_t xremote_device_page_start(uint16_t selected) {
    return (selected / XREMOTE_DEVICE_PAGE_SIZE) * XREMOTE_DEVICE_PAGE_SIZE;
}

static uint16_t xremote_device_page_count(uint16_t command_count) {
    return command_count == 0U ?
               0U :
               (command_count + XREMOTE_DEVICE_PAGE_SIZE - 1U) / XREMOTE_DEVICE_PAGE_SIZE;
}

static uint16_t xremote_device_page_last(uint16_t command_count, uint16_t page_start) {
    const uint16_t last = page_start + XREMOTE_DEVICE_PAGE_SIZE - 1U;
    return last < command_count ? last : command_count - 1U;
}

static bool
    xremote_device_grid_move(XRemoteDeviceContext* context, InputKey key, uint16_t command_count) {
    const uint16_t previous = context->selected;
    const uint16_t page_start = xremote_device_page_start(context->selected);
    const uint16_t local = context->selected - page_start;
    const uint16_t column = local % XREMOTE_DEVICE_GRID_COLUMNS;

    if(key == InputKeyLeft) {
        if(column > 0U) {
            context->selected--;
        } else if(page_start > 0U) {
            context->selected = page_start - 1U;
        }
    } else if(key == InputKeyRight) {
        if(column + 1U < XREMOTE_DEVICE_GRID_COLUMNS && context->selected + 1U < command_count &&
           context->selected + 1U < page_start + XREMOTE_DEVICE_PAGE_SIZE) {
            context->selected++;
        } else if(page_start + XREMOTE_DEVICE_PAGE_SIZE < command_count) {
            context->selected = page_start + XREMOTE_DEVICE_PAGE_SIZE;
        }
    } else if(key == InputKeyUp) {
        if(local >= XREMOTE_DEVICE_GRID_COLUMNS) {
            context->selected -= XREMOTE_DEVICE_GRID_COLUMNS;
        } else if(page_start > 0U) {
            const uint16_t previous_page = page_start - XREMOTE_DEVICE_PAGE_SIZE;
            const uint16_t candidate = previous_page + XREMOTE_DEVICE_GRID_COLUMNS + column;
            context->selected = candidate < command_count ?
                                    candidate :
                                    xremote_device_page_last(command_count, previous_page);
        }
    } else if(key == InputKeyDown) {
        const uint16_t candidate = context->selected + XREMOTE_DEVICE_GRID_COLUMNS;
        if(local < XREMOTE_DEVICE_GRID_COLUMNS && candidate < command_count &&
           candidate < page_start + XREMOTE_DEVICE_PAGE_SIZE) {
            context->selected = candidate;
        } else if(page_start + XREMOTE_DEVICE_PAGE_SIZE < command_count) {
            const uint16_t next_page = page_start + XREMOTE_DEVICE_PAGE_SIZE;
            const uint16_t next_candidate = next_page + column;
            context->selected = next_candidate < command_count ? next_candidate :
                                                                 command_count - 1U;
        }
    }
    return context->selected != previous;
}

static bool
    xremote_device_list_move(XRemoteDeviceContext* context, InputKey key, uint16_t command_count) {
    const uint16_t previous = context->selected;
    const uint16_t page_start = xremote_device_page_start(context->selected);
    const uint16_t local = context->selected - page_start;

    if(key == InputKeyUp) {
        if(context->selected > 0U) context->selected--;
    } else if(key == InputKeyDown) {
        if(context->selected + 1U < command_count) context->selected++;
    } else if(key == InputKeyLeft) {
        if(page_start > 0U) {
            const uint16_t previous_page = page_start - XREMOTE_DEVICE_PAGE_SIZE;
            const uint16_t candidate = previous_page + local;
            context->selected = candidate < command_count ?
                                    candidate :
                                    xremote_device_page_last(command_count, previous_page);
        }
    } else if(key == InputKeyRight) {
        const uint16_t next_page = page_start + XREMOTE_DEVICE_PAGE_SIZE;
        if(next_page < command_count) {
            const uint16_t candidate = next_page + local;
            context->selected = candidate < command_count ? candidate : command_count - 1U;
        }
    }
    return context->selected != previous;
}

static void xremote_device_runtime_update_selection_status(XRemoteDeviceContext* context) {
    uint8_t rf_index = 0U;
    context->status[0] = '\0';
    if(xremote_device_selected_is_rf(context, &rf_index) &&
       context->rf_status[rf_index] != XRemoteSubGhzStatusOk) {
        snprintf(
            context->status,
            sizeof(context->status),
            "%s",
            xremote_subghz_status_name(context->rf_status[rf_index]));
    }
}

static void xremote_device_context_unload(XRemoteDeviceContext* context) {
    if(context->ir_remote) {
        infrared_remote_free(context->ir_remote);
        context->ir_remote = NULL;
    }
    xremote_device_profile_reset(context->profile);
    memset(context->variants, 0, sizeof(context->variants));
    for(uint8_t index = 0U; index < XREMOTE_DEVICE_RF_COMMAND_MAX; index++) {
        context->rf_status[index] = XRemoteSubGhzStatusInvalidFile;
    }
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
        context->rf_status[index] =
            xremote_subghz_inspect(context->storage, profile->rf[index].path, &info);
        if(context->rf_status[index] == XRemoteSubGhzStatusOk) {
            context->variants[index] = info.original_variant;
            if(info.changing_code) {
                context->rf_status[index] = XRemoteSubGhzStatusChangingCodeBlocked;
            }
        }
    }
    if(xremote_device_command_count(context) == 0U) {
        snprintf(context->status, sizeof(context->status), "No usable commands");
        return false;
    }
    xremote_device_runtime_update_selection_status(context);
    return true;
}

static void xremote_device_runtime_draw_cell(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    const char* label,
    bool selected) {
    if(selected) {
        canvas_draw_rbox(canvas, x, y, width, height, 2);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, x, y, width, height, 2);
    }

    canvas_set_font(canvas, FontSecondary);
    xremote_device_draw_fitted(
        canvas,
        (int32_t)x + (int32_t)width / 2,
        (int32_t)y + (int32_t)height - 4,
        width - 6U,
        AlignCenter,
        label);
    canvas_set_color(canvas, ColorBlack);
}

static void xremote_device_runtime_command_label(
    const XRemoteDeviceContext* context,
    uint16_t command_index,
    char* output,
    size_t output_size) {
    const char* name = xremote_device_command_name(context, command_index);
    uint8_t rf_index = 0U;
    const bool unavailable = xremote_device_command_is_rf(context, command_index, &rf_index) &&
                             context->rf_status[rf_index] != XRemoteSubGhzStatusOk;
    snprintf(output, output_size, unavailable ? "! %s" : "%s", name);
}

static void xremote_device_runtime_draw(Canvas* canvas, void* model_context) {
    XRemoteViewModel* model = model_context;
    XRemoteDeviceContext* context = model->context;
    const ViewOrientation orientation = context->app_ctx->app_settings->orientation;
    const uint16_t count = xremote_device_command_count(context);
    const uint16_t page_start = xremote_device_page_start(context->selected);
    const uint16_t page = context->selected / XREMOTE_DEVICE_PAGE_SIZE;
    const uint16_t page_count = xremote_device_page_count(count);
    const bool is_rf = xremote_device_selected_is_rf(context, NULL);
    const char* radio = context->profile->radio == XRemoteDeviceRadioExternal ? "EXT" : "INT";
    char page_source[20];
    snprintf(
        page_source,
        sizeof(page_source),
        "%u/%u %s%s",
        (unsigned int)(page + 1U),
        (unsigned int)page_count,
        is_rf ? "RF " : "",
        is_rf ? radio : "IR");
    const char* title = context->status[0] != '\0' ? context->status : context->profile->name;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(orientation == ViewOrientationHorizontal) {
        xremote_device_draw_fitted(canvas, 2, 9, 82, AlignLeft, title);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, page_source);
        canvas_draw_line(canvas, 2, 12, 126, 12);

        for(uint8_t slot = 0U; slot < XREMOTE_DEVICE_PAGE_SIZE; slot++) {
            const uint16_t command_index = page_start + slot;
            if(command_index >= count) break;
            char label[40];
            xremote_device_runtime_command_label(context, command_index, label, sizeof(label));
            const uint8_t column = slot % XREMOTE_DEVICE_GRID_COLUMNS;
            const uint8_t row = slot / XREMOTE_DEVICE_GRID_COLUMNS;
            xremote_device_runtime_draw_cell(
                canvas,
                column == 0U ? 2U : 65U,
                row == 0U ? 15U : 32U,
                61U,
                15U,
                label,
                command_index == context->selected);
        }

        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Send");
    } else {
        xremote_device_draw_fitted(canvas, 2, 10, 39, AlignLeft, title);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 62, 10, AlignRight, AlignBottom, page_source);
        canvas_draw_line(canvas, 2, 13, 62, 13);

        for(uint8_t slot = 0U; slot < XREMOTE_DEVICE_PAGE_SIZE; slot++) {
            const uint16_t command_index = page_start + slot;
            if(command_index >= count) break;
            char label[40];
            xremote_device_runtime_command_label(context, command_index, label, sizeof(label));
            xremote_device_runtime_draw_cell(
                canvas,
                2U,
                (uint8_t)(17U + slot * 18U),
                60U,
                15U,
                label,
                command_index == context->selected);
        }

        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Send");
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
    if(status == XRemoteSubGhzStatusMissingFile || status == XRemoteSubGhzStatusInvalidFile ||
       status == XRemoteSubGhzStatusUnsupportedPreset ||
       status == XRemoteSubGhzStatusChangingCodeBlocked) {
        context->rf_status[rf_index] = status;
    }
    snprintf(context->status, sizeof(context->status), "%s", xremote_subghz_status_name(status));
    if(status == XRemoteSubGhzStatusOk) {
        dolphin_deed(DolphinDeedSubGhzSend);
        xremote_app_context_notify_led(context->app_ctx);
    }
}

static void xremote_device_runtime_toggle_radio(XRemoteDeviceContext* context) {
    if(!xremote_device_selected_is_rf(context, NULL)) {
        snprintf(context->status, sizeof(context->status), "IR uses IR output");
        return;
    }

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

static bool xremote_device_runtime_input(InputEvent* event, void* view_context) {
    XRemoteView* view = view_context;
    XRemoteDeviceContext* context = xremote_view_get_context(view);
    if(event->type == InputTypeShort && event->key == InputKeyBack) return false;

    const uint16_t count = xremote_device_command_count(context);
    if(count == 0U) return true;

    if(event->type == InputTypeShort &&
       (event->key == InputKeyUp || event->key == InputKeyDown || event->key == InputKeyLeft ||
        event->key == InputKeyRight)) {
        const bool moved = context->app_ctx->app_settings->orientation ==
                                   ViewOrientationHorizontal ?
                               xremote_device_grid_move(context, event->key, count) :
                               xremote_device_list_move(context, event->key, count);
        if(moved) xremote_device_runtime_update_selection_status(context);
    } else if(event->type == InputTypeShort && event->key == InputKeyOk) {
        xremote_device_runtime_send(context);
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        xremote_device_runtime_toggle_radio(context);
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

static bool xremote_device_library_has_extension(const char* filename) {
    if(!filename) return false;
    const size_t filename_length = strlen(filename);
    const size_t extension_length = strlen(XREMOTE_DEVICE_PROFILE_EXTENSION);
    return filename_length > extension_length && strcmp(
                                                     filename + filename_length - extension_length,
                                                     XREMOTE_DEVICE_PROFILE_EXTENSION) == 0;
}

static bool xremote_device_library_entry_path(
    const XRemoteDeviceLibraryEntry* entry,
    char* output,
    size_t output_size) {
    if(!entry || !output || output_size == 0U || entry->filename[0] == '\0') return false;
    const int written =
        snprintf(output, output_size, "%s/%s", XREMOTE_DEVICE_PROFILE_FOLDER, entry->filename);
    return written > 0 && (size_t)written < output_size;
}

static bool xremote_device_library_load_entry(
    XRemoteDeviceContext* context,
    uint8_t index,
    XRemoteDeviceProfile* profile) {
    if(!context || !profile || index >= context->library_count) return false;
    char path[XREMOTE_DEVICE_PATH_MAX];
    return xremote_device_library_entry_path(&context->library[index], path, sizeof(path)) &&
           xremote_device_profile_load(context->storage, path, profile);
}

static uint16_t
    xremote_device_library_ir_count(Storage* storage, const char* path, bool* missing) {
    if(!path || path[0] == '\0') return 0U;
    if(!storage_file_exists(storage, path)) {
        *missing = true;
        return 0U;
    }

    FuriString* source_path = furi_string_alloc_set_str(path);
    InfraredRemote* remote = infrared_remote_alloc();
    const bool loaded = remote && infrared_remote_load(remote, source_path);
    const uint16_t count = loaded ? infrared_remote_get_button_count(remote) : 0U;
    if(!loaded || count == 0U) *missing = true;
    if(remote) infrared_remote_free(remote);
    furi_string_free(source_path);
    return count;
}

static void xremote_device_library_sort(XRemoteDeviceContext* context) {
    for(uint8_t index = 1U; index < context->library_count; index++) {
        XRemoteDeviceLibraryEntry moving = context->library[index];
        uint8_t destination = index;
        while(destination > 0U &&
              strcmp(context->library[destination - 1U].name, moving.name) > 0) {
            context->library[destination] = context->library[destination - 1U];
            destination--;
        }
        context->library[destination] = moving;
    }
}

static void xremote_device_library_keep_visible(XRemoteDeviceContext* context) {
    if(context->library_count == 0U) {
        context->library_selected = 0U;
        context->library_top = 0U;
        return;
    }
    if(context->library_selected >= context->library_count) {
        context->library_selected = context->library_count - 1U;
    }
    if(context->library_selected < context->library_top) {
        context->library_top = context->library_selected;
    } else if(context->library_selected >= context->library_top + XREMOTE_DEVICE_LIBRARY_ROWS) {
        context->library_top = context->library_selected - XREMOTE_DEVICE_LIBRARY_ROWS + 1U;
    }
}

static void xremote_device_library_refresh(XRemoteDeviceContext* context) {
    context->library_count = 0U;
    context->library_truncated = false;
    if(!xremote_device_profile_storage_ready(context->storage)) {
        snprintf(context->library_notice, sizeof(context->library_notice), "Storage unavailable");
        return;
    }

    File* directory = storage_file_alloc(context->storage);
    FileInfo info;
    char filename[XREMOTE_DEVICE_LIBRARY_FILENAME_MAX];
    if(storage_dir_open(directory, XREMOTE_DEVICE_PROFILE_FOLDER)) {
        while(storage_dir_read(directory, &info, filename, sizeof(filename))) {
            if(file_info_is_dir(&info) || !xremote_device_library_has_extension(filename)) {
                continue;
            }
            if(context->library_count >= XREMOTE_DEVICE_LIBRARY_MAX) {
                context->library_truncated = true;
                continue;
            }

            XRemoteDeviceLibraryEntry* entry = &context->library[context->library_count];
            memset(entry, 0, sizeof(*entry));
            snprintf(entry->filename, sizeof(entry->filename), "%s", filename);

            char path[XREMOTE_DEVICE_PATH_MAX];
            XRemoteDeviceProfile* profile = xremote_device_profile_alloc();
            const bool path_ready = xremote_device_library_entry_path(entry, path, sizeof(path));
            const bool loaded = profile && path_ready &&
                                xremote_device_profile_load(context->storage, path, profile);
            if(!loaded) {
                xremote_device_basename(filename, entry->name, sizeof(entry->name));
                entry->health = XRemoteDeviceLibraryInvalid;
            } else {
                snprintf(entry->name, sizeof(entry->name), "%s", profile->name);
                entry->rf_count = profile->rf_count;
                bool missing = false;
                entry->ir_count =
                    xremote_device_library_ir_count(context->storage, profile->ir_path, &missing);
                for(uint8_t index = 0U; index < profile->rf_count; index++) {
                    if(!storage_file_exists(context->storage, profile->rf[index].path)) {
                        missing = true;
                    }
                }
                entry->health = missing ? XRemoteDeviceLibraryMissing : XRemoteDeviceLibraryReady;
            }
            xremote_device_profile_free(profile);
            context->library_count++;
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);

    xremote_device_library_sort(context);
    xremote_device_library_keep_visible(context);
    if(context->library_truncated) {
        snprintf(
            context->library_notice,
            sizeof(context->library_notice),
            "Showing first %u profiles",
            XREMOTE_DEVICE_LIBRARY_MAX);
    } else {
        context->library_notice[0] = '\0';
    }
}

static const char* xremote_device_library_health_name(XRemoteDeviceLibraryHealth health) {
    if(health == XRemoteDeviceLibraryMissing) return "MISS";
    if(health == XRemoteDeviceLibraryInvalid) return "BAD";
    return "OK";
}

static const char* xremote_device_library_action_name(uint8_t action) {
    static const char* const names[XRemoteDeviceLibraryActionCount] = {
        "Open",
        "Edit",
        "Repair",
        "Copy",
        "Export",
        "Delete",
    };
    return action < XRemoteDeviceLibraryActionCount ? names[action] : "?";
}

static void xremote_device_library_draw_entry(
    Canvas* canvas,
    const XRemoteDeviceLibraryEntry* entry,
    uint8_t y,
    bool selected,
    bool vertical) {
    if(selected) {
        canvas_draw_rbox(canvas, 2, y, vertical ? 60U : 124U, vertical ? 22U : 11U, 2);
        canvas_set_color(canvas, ColorWhite);
    }

    canvas_set_font(canvas, FontSecondary);
    char summary[24];
    snprintf(
        summary,
        sizeof(summary),
        "I%u/R%u %s",
        (unsigned int)entry->ir_count,
        (unsigned int)entry->rf_count,
        xremote_device_library_health_name(entry->health));
    if(vertical) {
        xremote_device_draw_fitted(canvas, 5, y + 8U, 54U, AlignLeft, entry->name);
        canvas_draw_str_aligned(canvas, 59, y + 19U, AlignRight, AlignBottom, summary);
    } else {
        xremote_device_draw_fitted(canvas, 5, y + 8U, 58U, AlignLeft, entry->name);
        canvas_draw_str_aligned(canvas, 124, y + 8U, AlignRight, AlignBottom, summary);
    }
    canvas_set_color(canvas, ColorBlack);
}

static void xremote_device_library_draw_actions(Canvas* canvas, XRemoteDeviceContext* context) {
    const XRemoteDeviceLibraryEntry* entry = &context->library[context->library_selected];
    const bool vertical = context->app_ctx->app_settings->orientation == ViewOrientationVertical;
    xremote_device_draw_fitted(
        canvas, 2, vertical ? 10 : 9, vertical ? 40U : 94U, AlignLeft, entry->name);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        vertical ? 62 : 126,
        vertical ? 10 : 9,
        AlignRight,
        AlignBottom,
        xremote_device_library_health_name(entry->health));
    canvas_draw_line(canvas, 2, vertical ? 13 : 12, vertical ? 62 : 126, vertical ? 13 : 12);

    for(uint8_t action = 0U; action < XRemoteDeviceLibraryActionCount; action++) {
        if(vertical) {
            xremote_device_runtime_draw_cell(
                canvas,
                2U,
                (uint8_t)(16U + action * 16U),
                60U,
                14U,
                xremote_device_library_action_name(action),
                context->library_action == action);
        } else {
            const uint8_t column = action % 2U;
            const uint8_t row = action / 2U;
            xremote_device_runtime_draw_cell(
                canvas,
                column == 0U ? 2U : 65U,
                (uint8_t)(15U + row * 13U),
                61U,
                11U,
                xremote_device_library_action_name(action),
                context->library_action == action);
        }
    }
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Select");
}

static void xremote_device_library_draw(Canvas* canvas, void* model_context) {
    XRemoteViewModel* model = model_context;
    XRemoteDeviceContext* context = model->context;
    const bool vertical = context->app_ctx->app_settings->orientation == ViewOrientationVertical;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(context->library_actions && context->library_count > 0U) {
        xremote_device_library_draw_actions(canvas, context);
        return;
    }

    xremote_device_draw_fitted(
        canvas, 2, vertical ? 10 : 9, vertical ? 44U : 100U, AlignLeft, "Profile Library");
    canvas_set_font(canvas, FontSecondary);
    char count[12];
    snprintf(
        count,
        sizeof(count),
        "%u%s",
        (unsigned int)context->library_count,
        context->library_truncated ? "+" : "");
    canvas_draw_str_aligned(
        canvas, vertical ? 62 : 126, vertical ? 10 : 9, AlignRight, AlignBottom, count);
    canvas_draw_line(canvas, 2, vertical ? 13 : 12, vertical ? 62 : 126, vertical ? 13 : 12);

    if(context->library_count == 0U) {
        canvas_draw_str_aligned(
            canvas,
            vertical ? 32 : 64,
            vertical ? 39 : 28,
            AlignCenter,
            AlignCenter,
            context->library_notice[0] != '\0' ? context->library_notice : "No device profiles");
        canvas_draw_str_aligned(
            canvas,
            vertical ? 32 : 64,
            vertical ? 56 : 41,
            AlignCenter,
            AlignCenter,
            vertical ? "Import IR / RF" : "Import IR or create RF");
        elements_button_left(canvas, "Back");
        return;
    }

    for(uint8_t row = 0U; row < XREMOTE_DEVICE_LIBRARY_ROWS; row++) {
        const uint8_t index = context->library_top + row;
        if(index >= context->library_count) break;
        xremote_device_library_draw_entry(
            canvas,
            &context->library[index],
            (uint8_t)((vertical ? 17U : 15U) + row * (vertical ? 25U : 12U)),
            index == context->library_selected,
            vertical);
    }
    elements_button_left(canvas, "Back");
    elements_button_center(canvas, "Open");
    elements_button_right(canvas, "More");
}

static bool xremote_device_library_start_runtime(XRemoteDeviceContext* context) {
    XRemoteDeviceProfile* selected = xremote_device_profile_alloc();
    if(!selected) {
        xremote_device_message(context, "Profile Library", "Out of memory.");
        return false;
    }
    const bool loaded =
        xremote_device_library_load_entry(context, context->library_selected, selected);
    const bool ready = loaded && xremote_device_context_load(context, selected);
    if(ready) {
        xremote_app_view_free(context->owner_app);
        xremote_app_view_alloc2(
            context->owner_app, XRemoteViewDeviceRuntime, xremote_device_runtime_alloc, context);
        xremote_app_view_set_previous_callback(
            context->owner_app, xremote_device_previous_library);
        xremote_app_switch_to_view(context->owner_app, XRemoteViewDeviceRuntime);
    } else {
        xremote_device_message(
            context, "Profile Library", loaded ? "No usable commands." : "Profile is invalid.");
    }
    xremote_device_profile_free(selected);
    return ready;
}

static bool xremote_device_library_start_editor(XRemoteDeviceContext* context) {
    XRemoteDeviceProfile* selected = xremote_device_profile_alloc();
    if(!selected) {
        xremote_device_message(context, "Profile Library", "Out of memory.");
        return false;
    }
    const bool loaded =
        xremote_device_library_load_entry(context, context->library_selected, selected);
    if(loaded) {
        xremote_device_context_unload(context);
        *context->profile = *selected;
        context->editor_selected = 0U;
        context->status[0] = '\0';
        xremote_app_view_free(context->owner_app);
        xremote_app_view_alloc2(
            context->owner_app, XRemoteViewDeviceEditor, xremote_device_editor_alloc, context);
        xremote_app_view_set_previous_callback(
            context->owner_app, xremote_device_previous_library);
        xremote_app_switch_to_view(context->owner_app, XRemoteViewDeviceEditor);
    } else {
        xremote_device_message(context, "Profile Library", "Profile is invalid.");
    }
    xremote_device_profile_free(selected);
    return loaded;
}

static void xremote_device_library_duplicate(XRemoteDeviceContext* context) {
    XRemoteDeviceProfile* source = xremote_device_profile_alloc();
    XRemoteDeviceProfile* duplicate = xremote_device_profile_alloc();
    const bool loaded =
        source && duplicate &&
        xremote_device_library_load_entry(context, context->library_selected, source);
    const bool copied = loaded &&
                        xremote_device_profile_duplicate(context->storage, source, duplicate);
    xremote_device_profile_free(duplicate);
    xremote_device_profile_free(source);
    xremote_device_library_refresh(context);
    xremote_device_message(
        context,
        "Duplicate profile",
        copied ? "Independent profile copy created.\nSources stay unchanged." :
                 "Cannot duplicate profile.");
}

static void xremote_device_library_export(XRemoteDeviceContext* context) {
    XRemoteDeviceProfile* profile = xremote_device_profile_alloc();
    if(!profile ||
       !xremote_device_library_load_entry(context, context->library_selected, profile)) {
        xremote_device_profile_free(profile);
        xremote_device_message(context, "Export bundle", "Profile is invalid.");
        return;
    }

    char manifest_path[XREMOTE_DEVICE_PATH_MAX];
    const XRemoteDeviceBundleStatus status = xremote_device_bundle_export(
        context->storage, profile, manifest_path, sizeof(manifest_path));
    xremote_device_profile_free(profile);
    if(status != XRemoteDeviceBundleStatusOk) {
        xremote_device_message(
            context, "Export bundle", xremote_device_bundle_status_name(status));
        return;
    }

    char folder_name[XREMOTE_DEVICE_NAME_MAX + 1U];
    char folder_path[XREMOTE_DEVICE_PATH_MAX];
    snprintf(folder_path, sizeof(folder_path), "%s", manifest_path);
    char* manifest_leaf = strrchr(folder_path, '/');
    if(manifest_leaf) *manifest_leaf = '\0';
    xremote_device_basename(folder_path, folder_name, sizeof(folder_name));
    char message[64];
    snprintf(message, sizeof(message), "Saved in bundles/%s", folder_name);
    xremote_device_message(context, "Export bundle", message);
}

static bool
    xremote_device_library_repair_ir(XRemoteDeviceContext* context, XRemoteDeviceProfile* profile) {
    FuriString* selected_path = furi_string_alloc();
    const bool selected = xremote_device_editor_select_ir(context, selected_path);
    const bool updated =
        selected && xremote_device_profile_set_ir(profile, furi_string_get_cstr(selected_path)) &&
        xremote_device_profile_store(context->storage, profile);
    furi_string_free(selected_path);
    if(selected && !updated) {
        xremote_device_message(context, "Repair IR", "Save failed;\nprofile unchanged.");
    }
    return updated;
}

static bool xremote_device_library_repair_rf(
    XRemoteDeviceContext* context,
    XRemoteDeviceProfile* profile,
    uint8_t index) {
    FuriString* selected_path = furi_string_alloc();
    if(!xremote_device_select_file(
           context, selected_path, EXT_PATH("subghz"), ".sub", &I_sub1_10px)) {
        furi_string_free(selected_path);
        return false;
    }

    XRemoteSubGhzInfo info;
    const XRemoteSubGhzStatus status =
        xremote_subghz_inspect(context->storage, furi_string_get_cstr(selected_path), &info);
    if(status != XRemoteSubGhzStatusOk) {
        xremote_device_message(context, "Repair Sub-GHz", xremote_subghz_status_name(status));
        furi_string_free(selected_path);
        return false;
    }
    if(info.changing_code) {
        xremote_device_message(
            context, "Repair Sub-GHz", "Changing-code signal\nis not supported.");
        furi_string_free(selected_path);
        return false;
    }

    const bool updated =
        xremote_device_profile_replace_rf_source(
            profile, index, furi_string_get_cstr(selected_path), info.protocol, info.adapter) &&
        xremote_device_profile_store(context->storage, profile);
    furi_string_free(selected_path);
    if(!updated) {
        xremote_device_message(context, "Repair Sub-GHz", "Save failed;\nprofile unchanged.");
    }
    return updated;
}

static void xremote_device_library_repair(XRemoteDeviceContext* context) {
    XRemoteDeviceProfile* profile = xremote_device_profile_alloc();
    if(!profile ||
       !xremote_device_library_load_entry(context, context->library_selected, profile)) {
        xremote_device_profile_free(profile);
        xremote_device_message(context, "Repair profile", "Profile is invalid.");
        return;
    }

    bool attempted = false;
    bool repaired = false;
    if(profile->ir_path[0] != '\0') {
        bool ir_missing = false;
        xremote_device_library_ir_count(context->storage, profile->ir_path, &ir_missing);
        if(ir_missing) {
            attempted = true;
            repaired = xremote_device_library_repair_ir(context, profile);
        }
    }
    if(!attempted) {
        for(uint8_t index = 0U; index < profile->rf_count; index++) {
            if(!storage_file_exists(context->storage, profile->rf[index].path)) {
                attempted = true;
                repaired = xremote_device_library_repair_rf(context, profile, index);
                break;
            }
        }
    }

    xremote_device_profile_free(profile);
    if(!attempted) {
        xremote_device_message(context, "Repair profile", "No missing source found.");
        return;
    }
    if(!repaired) return;

    xremote_device_library_refresh(context);
    const bool ready = context->library_selected < context->library_count &&
                       context->library[context->library_selected].health ==
                           XRemoteDeviceLibraryReady;
    xremote_device_message(
        context,
        "Repair profile",
        ready ? "Source repaired.\nProfile is ready." : "Source repaired.\nMore sources missing.");
}

static void xremote_device_library_delete(XRemoteDeviceContext* context) {
    if(context->library_selected >= context->library_count) return;
    const XRemoteDeviceLibraryEntry* entry = &context->library[context->library_selected];
    char prompt[64];
    snprintf(prompt, sizeof(prompt), "Delete %.28s?\nSources stay on SD.", entry->name);
    if(!xremote_device_confirm(context, "Delete profile?", prompt, "Delete")) return;

    char path[XREMOTE_DEVICE_PATH_MAX];
    const bool path_ready = xremote_device_library_entry_path(entry, path, sizeof(path));
    const bool deleted = path_ready && xremote_device_profile_delete_path(context->storage, path);
    xremote_device_library_refresh(context);
    xremote_device_message(
        context,
        "Delete profile",
        deleted ? "Profile deleted.\nSources stay unchanged." : "Delete failed.");
}

static void xremote_device_library_execute(XRemoteDeviceContext* context, uint8_t action) {
    if(context->library_selected >= context->library_count) return;
    const XRemoteDeviceLibraryEntry* entry = &context->library[context->library_selected];
    if(entry->health == XRemoteDeviceLibraryInvalid &&
       action != XRemoteDeviceLibraryActionDelete) {
        xremote_device_message(
            context, "Invalid profile", "Only Delete is available\nfor this file.");
        return;
    }
    if(action == XRemoteDeviceLibraryActionRepair &&
       entry->health != XRemoteDeviceLibraryMissing) {
        xremote_device_message(
            context, "Repair profile", "Repair is available only\nfor missing sources.");
        return;
    }
    if(action == XRemoteDeviceLibraryActionExport && entry->health != XRemoteDeviceLibraryReady) {
        xremote_device_message(context, "Export bundle", "Export requires a Ready profile.");
        return;
    }

    context->library_actions = false;
    if(action == XRemoteDeviceLibraryActionOpen) {
        xremote_device_library_start_runtime(context);
    } else if(action == XRemoteDeviceLibraryActionEdit) {
        xremote_device_library_start_editor(context);
    } else if(action == XRemoteDeviceLibraryActionRepair) {
        xremote_device_library_repair(context);
    } else if(action == XRemoteDeviceLibraryActionDuplicate) {
        xremote_device_library_duplicate(context);
    } else if(action == XRemoteDeviceLibraryActionExport) {
        xremote_device_library_export(context);
    } else if(action == XRemoteDeviceLibraryActionDelete) {
        xremote_device_library_delete(context);
    }
}

static bool xremote_device_library_input(InputEvent* event, void* view_context) {
    XRemoteView* view = view_context;
    XRemoteDeviceContext* context = xremote_view_get_context(view);
    if(event->type != InputTypeShort) return true;

    if(event->key == InputKeyBack) {
        if(context->library_actions) {
            context->library_actions = false;
        } else {
            return false;
        }
    } else if(context->library_count == 0U) {
        return true;
    } else if(context->library_actions) {
        const bool vertical = context->app_ctx->app_settings->orientation ==
                              ViewOrientationVertical;
        if(vertical && event->key == InputKeyUp && context->library_action > 0U) {
            context->library_action--;
        } else if(
            vertical && event->key == InputKeyDown &&
            context->library_action + 1U < XRemoteDeviceLibraryActionCount) {
            context->library_action++;
        } else if(!vertical && event->key == InputKeyLeft && (context->library_action % 2U) != 0U) {
            context->library_action--;
        } else if(
            !vertical && event->key == InputKeyRight && (context->library_action % 2U) == 0U &&
            context->library_action + 1U < XRemoteDeviceLibraryActionCount) {
            context->library_action++;
        } else if(!vertical && event->key == InputKeyUp && context->library_action >= 2U) {
            context->library_action -= 2U;
        } else if(
            !vertical && event->key == InputKeyDown &&
            context->library_action + 2U < XRemoteDeviceLibraryActionCount) {
            context->library_action += 2U;
        } else if(event->key == InputKeyOk) {
            xremote_device_library_execute(context, context->library_action);
            return true;
        }
    } else if(event->key == InputKeyUp) {
        if(context->library_selected > 0U) context->library_selected--;
        xremote_device_library_keep_visible(context);
    } else if(event->key == InputKeyDown) {
        if(context->library_selected + 1U < context->library_count) {
            context->library_selected++;
        }
        xremote_device_library_keep_visible(context);
    } else if(event->key == InputKeyOk) {
        xremote_device_library_execute(context, XRemoteDeviceLibraryActionOpen);
        return true;
    } else if(event->key == InputKeyRight) {
        context->library_actions = true;
        context->library_action = XRemoteDeviceLibraryActionOpen;
    }

    with_view_model(
        xremote_view_get_view(view), XRemoteViewModel * model, { model->context = context; }, true);
    return true;
}

static void xremote_device_library_show(XRemoteDeviceContext* context) {
    context->library_actions = false;
    xremote_device_library_refresh(context);
    with_view_model(
        xremote_view_get_view(context->library_view),
        XRemoteViewModel * model,
        { model->context = context; },
        true);
    view_dispatcher_switch_to_view(context->app_ctx->view_dispatcher, XRemoteViewDeviceLibrary);
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

static void xremote_device_import_bundle(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    FuriString* path = furi_string_alloc();
    if(!xremote_device_select_file(
           context,
           path,
           XREMOTE_DEVICE_BUNDLE_FOLDER,
           XREMOTE_DEVICE_BUNDLE_EXTENSION,
           &I_unknown_10px)) {
        furi_string_free(path);
        return;
    }

    XRemoteDeviceProfile* imported = xremote_device_profile_alloc();
    if(!imported) {
        furi_string_free(path);
        xremote_device_message(context, "Import bundle", "Out of memory.");
        return;
    }
    const XRemoteDeviceBundleStatus status =
        xremote_device_bundle_import(context->storage, furi_string_get_cstr(path), imported);
    if(status == XRemoteDeviceBundleStatusOk) {
        char message[64];
        snprintf(message, sizeof(message), "Imported as %.32s", imported->name);
        xremote_device_message(context, "Import bundle", message);
    } else {
        xremote_device_message(
            context, "Import bundle", xremote_device_bundle_status_name(status));
    }
    xremote_device_profile_free(imported);
    furi_string_free(path);
}

static void xremote_device_guide(XRemoteApp* app) {
    XRemoteDeviceContext* context = app->context;
    xremote_device_message(
        context,
        "Device Profiles",
        "Library: OK opens, Right\n"
        "has Edit/Repair/Export.\n"
        "Repair fixes one source.\n"
        "Bundles include all files.\n"
        "Import IR or RF, then mix.\n"
        "Runtime: arrows select,\n"
        "OK sends, hold OK INT/EXT.");
}

static void xremote_device_menu_callback(void* callback_context, uint32_t index) {
    XRemoteApp* app = callback_context;
    switch(index) {
    case XRemoteDeviceMenuLibrary:
        xremote_device_library_show(app->context);
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
    case XRemoteDeviceMenuImportBundle:
        xremote_device_import_bundle(app);
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
    if(context->library_view) {
        view_dispatcher_remove_view(context->app_ctx->view_dispatcher, XRemoteViewDeviceLibrary);
        xremote_view_free(context->library_view);
    }
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
    if(!xremote_device_profile_storage_ready(context->storage)) {
        snprintf(context->status, sizeof(context->status), "Storage unavailable");
    }

    XRemoteApp* app = xremote_app_alloc(app_ctx);
    context->owner_app = app;
    xremote_app_set_user_context(app, context, xremote_device_context_free);
    xremote_app_submenu_alloc(app, XRemoteViewDeviceProfiles, xremote_device_previous_main);
    xremote_app_submenu_add(
        app, "Profile Library", XRemoteDeviceMenuLibrary, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "Import IR Remote", XRemoteDeviceMenuImportIr, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "New RF Profile", XRemoteDeviceMenuImportRf, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "Add Sub-GHz", XRemoteDeviceMenuAddRf, xremote_device_menu_callback);
    xremote_app_submenu_add(
        app, "Import Bundle", XRemoteDeviceMenuImportBundle, xremote_device_menu_callback);
    xremote_app_submenu_add(app, "Guide", XRemoteDeviceMenuGuide, xremote_device_menu_callback);

    context->library_view =
        xremote_view_alloc(app_ctx, xremote_device_library_input, xremote_device_library_draw);
    xremote_view_set_context(context->library_view, context, NULL);
    xremote_view_model_context_set(context->library_view, context);
    View* library_view = xremote_view_get_view(context->library_view);
    view_set_previous_callback(library_view, xremote_device_library_previous);
    view_dispatcher_add_view(app_ctx->view_dispatcher, XRemoteViewDeviceLibrary, library_view);
    return app;
}
