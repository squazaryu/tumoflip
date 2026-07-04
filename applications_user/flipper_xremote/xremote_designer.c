/*!
 *  @file flipper-xremote/xremote_designer.c
 *  @license This project is released under the GNU GPLv3 License
 *
 * @brief On-device authoring wizard for XRemote-compatible IR files.
 */

#include "xremote_designer.h"

#include <ctype.h>
#include <string.h>

#define XREMOTE_DESIGNER_SOURCE_LIMIT 60
#define XREMOTE_DESIGNER_NAME_MAX     XREMOTE_APP_TEXT_MAX

typedef struct XRemoteDesignerContext XRemoteDesignerContext;

typedef struct {
    XRemoteDesignerContext* designer;
    uint8_t target_index;
} XRemoteDesignerItemContext;

struct XRemoteDesignerContext {
    XRemoteAppContext* app_ctx;
    InfraredRemote* source_remote;
    InfraredRemote* output_remote;
    VariableItemList* item_list;
    TextInput* text_input;
    DialogEx* dialog_ex;
    XRemoteDesignerItemContext item_contexts[XREMOTE_BUTTON_COUNT];
    uint8_t selected_source[XREMOTE_BUTTON_COUNT];
    char text_store[XREMOTE_DESIGNER_NAME_MAX + 1];
    char dialog_header[24];
    char dialog_text[96];
    XRemoteViewID dialog_return_view;
    XRemoteRemoteType remote_type;
    uint8_t source_count;
    bool map_view_added;
    bool dialog_view_added;
};

static void xremote_designer_show_load_error(const char* text) {
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    dialog_message_show_storage_error(dialogs, text);
    furi_record_close(RECORD_DIALOGS);
}

static void xremote_designer_dialog_callback(DialogExResult result, void* context) {
    UNUSED(result);
    XRemoteDesignerContext* ctx = context;
    xremote_app_assert_void(ctx);

    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, ctx->dialog_return_view);
}

static void xremote_designer_show_status(
    XRemoteDesignerContext* ctx,
    const char* header,
    const char* text,
    XRemoteViewID return_view) {
    xremote_app_assert_void(ctx);
    xremote_app_assert_void(ctx->dialog_ex);

    snprintf(ctx->dialog_header, sizeof(ctx->dialog_header), "%s", header);
    snprintf(ctx->dialog_text, sizeof(ctx->dialog_text), "%s", text);
    ctx->dialog_return_view = return_view;

    dialog_ex_reset(ctx->dialog_ex);
    dialog_ex_set_header(ctx->dialog_ex, ctx->dialog_header, 64, 7, AlignCenter, AlignTop);
    dialog_ex_set_text(ctx->dialog_ex, ctx->dialog_text, 64, 25, AlignCenter, AlignTop);
    dialog_ex_set_icon(ctx->dialog_ex, 0, 0, NULL);
    dialog_ex_set_left_button_text(ctx->dialog_ex, NULL);
    dialog_ex_set_center_button_text(ctx->dialog_ex, NULL);
    dialog_ex_set_right_button_text(ctx->dialog_ex, "OK");
    dialog_ex_set_result_callback(ctx->dialog_ex, xremote_designer_dialog_callback);
    dialog_ex_set_context(ctx->dialog_ex, ctx);

    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewDialogExit);
}

static const char* xremote_designer_source_name(
    XRemoteDesignerContext* ctx,
    uint8_t source_index) {
    if(source_index == 0 || source_index > ctx->source_count) return "None";

    InfraredRemoteButton* button = infrared_remote_get_button(ctx->source_remote, source_index - 1);
    return infrared_remote_button_get_name(button);
}

static uint8_t xremote_designer_mapped_count(XRemoteDesignerContext* ctx) {
    const uint32_t target_count = xremote_remote_type_get_button_count(ctx->remote_type);
    uint8_t mapped = 0;

    for(uint32_t i = 0; i < target_count; i++) {
        if(ctx->selected_source[i] != 0) mapped++;
    }

    return mapped;
}

static bool xremote_designer_sanitize_name(
    const char* input,
    char* output,
    size_t output_size) {
    size_t pos = 0;

    if(output_size == 0) return false;

    for(size_t i = 0; input[i] != '\0' && pos + 1 < output_size; i++) {
        const unsigned char c = (unsigned char)input[i];
        if(isalnum(c) || c == '_' || c == '-') {
            output[pos++] = (char)c;
        } else if(c == ' ') {
            output[pos++] = '_';
        }
    }

    output[pos] = '\0';
    return pos > 0;
}

static bool xremote_designer_store_output(XRemoteDesignerContext* ctx, const char* name) {
    char safe_name[XREMOTE_DESIGNER_NAME_MAX + 1];
    char output_file[256];

    if(xremote_designer_mapped_count(ctx) == 0) {
        xremote_designer_show_status(
            ctx, "Not saved", "Nothing mapped\nSelect commands first", XRemoteViewDesignerMap);
        return false;
    }

    if(!xremote_designer_sanitize_name(name, safe_name, sizeof(safe_name))) {
        xremote_designer_show_status(
            ctx, "Not saved", "Invalid remote\nname", XRemoteViewDesignerMap);
        return false;
    }

    int written = snprintf(
        output_file,
        sizeof(output_file),
        "%s/%s%s",
            XREMOTE_APP_FOLDER,
            safe_name,
            XREMOTE_APP_EXTENSION);
    if(written <= 0 || (size_t)written >= sizeof(output_file)) {
        xremote_designer_show_status(
            ctx, "Not saved", "Remote path\ntoo long", XRemoteViewDesignerMap);
        return false;
    }

    infrared_remote_reset(ctx->output_remote);
    infrared_remote_set_name(ctx->output_remote, safe_name);
    infrared_remote_set_path(ctx->output_remote, output_file);

    const uint32_t target_count = xremote_remote_type_get_button_count(ctx->remote_type);
    for(uint32_t i = 0; i < target_count; i++) {
        const uint8_t source_index = ctx->selected_source[i];
        if(source_index == 0) continue;

        InfraredRemoteButton* source_button =
            infrared_remote_get_button(ctx->source_remote, source_index - 1);
        InfraredSignal* signal = infrared_remote_button_get_signal(source_button);
        const char* target_name = xremote_remote_type_get_button_name(ctx->remote_type, i);
        infrared_remote_push_button(ctx->output_remote, target_name, signal);
    }

    if(!infrared_remote_store(ctx->output_remote)) {
        xremote_designer_show_status(
            ctx, "Not saved", "Cannot save\nremote file", XRemoteViewDesignerMap);
        return false;
    }

    FuriString* output_path = furi_string_alloc_set_str(output_file);
    XRemoteAppButtons* buttons = xremote_app_buttons_alloc();
    buttons->app_ctx = ctx->app_ctx;
    xremote_app_buttons_set_remote_type(buttons, ctx->remote_type);

    const bool extension_stored = xremote_app_extension_store(buttons, output_path);
    xremote_app_buttons_free(buttons);
    furi_string_free(output_path);

    if(!extension_stored) {
        xremote_designer_show_status(
            ctx, "Not saved", "Cannot save\nlayout data", XRemoteViewDesignerMap);
        return false;
    }

    xremote_app_context_notify_led(ctx->app_ctx);
    char status_text[96];
    snprintf(status_text, sizeof(status_text), "Saved as\n%.80s%s", safe_name, XREMOTE_APP_EXTENSION);
    xremote_designer_show_status(ctx, "Saved", status_text, XRemoteViewSubmenu);
    return true;
}

static void xremote_designer_text_input_callback(void* context) {
    XRemoteDesignerContext* ctx = context;
    xremote_app_assert_void(ctx);
    xremote_designer_store_output(ctx, ctx->text_store);
}

static uint32_t xremote_designer_text_input_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewDesignerMap;
}

static void xremote_designer_start_name_input(XRemoteDesignerContext* ctx) {
    xremote_app_assert_void(ctx);
    snprintf(ctx->text_store, sizeof(ctx->text_store), "Remote_");

    text_input_reset(ctx->text_input);
    text_input_set_header_text(ctx->text_input, "Export remote");
    text_input_set_result_callback(
        ctx->text_input,
        xremote_designer_text_input_callback,
        ctx,
        ctx->text_store,
        XREMOTE_DESIGNER_NAME_MAX,
        true);

    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewTextInput);
}

static void xremote_designer_assignment_changed(VariableItem* item) {
    XRemoteDesignerItemContext* item_ctx = variable_item_get_context(item);
    XRemoteDesignerContext* ctx = item_ctx->designer;
    const uint8_t source_index = variable_item_get_current_value_index(item);

    ctx->selected_source[item_ctx->target_index] = source_index;
    variable_item_set_current_value_text(item, xremote_designer_source_name(ctx, source_index));
}

static void xremote_designer_map_enter_callback(void* context, uint32_t index) {
    XRemoteDesignerContext* ctx = context;
    xremote_app_assert_void(ctx);

    const uint32_t target_count = xremote_remote_type_get_button_count(ctx->remote_type);
    if(index >= target_count) {
        xremote_designer_start_name_input(ctx);
        return;
    }

    const uint8_t source_index = ctx->selected_source[index];
    if(source_index == 0) {
        xremote_designer_show_status(
            ctx, "Preview", "No command\nmapped", XRemoteViewDesignerMap);
        return;
    }

    InfraredRemoteButton* source_button =
        infrared_remote_get_button(ctx->source_remote, source_index - 1);
    xremote_app_send_signal(ctx->app_ctx, infrared_remote_button_get_signal(source_button));
}

static uint32_t xremote_designer_map_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewDesigner;
}

static void xremote_designer_map_view_alloc(XRemoteDesignerContext* ctx) {
    variable_item_list_reset(ctx->item_list);
    memset(ctx->selected_source, 0, sizeof(ctx->selected_source));

    const uint32_t target_count = xremote_remote_type_get_button_count(ctx->remote_type);
    for(uint32_t i = 0; i < target_count; i++) {
        const char* target_name = xremote_remote_type_get_button_name(ctx->remote_type, i);
        size_t source_match = 0;
        uint8_t source_index = 0;

        if(infrared_remote_find_button_by_name(ctx->source_remote, target_name, &source_match) &&
           source_match < ctx->source_count) {
            source_index = source_match + 1;
        }

        ctx->selected_source[i] = source_index;
        ctx->item_contexts[i].designer = ctx;
        ctx->item_contexts[i].target_index = i;

        VariableItem* item = variable_item_list_add(
            ctx->item_list,
            target_name,
            ctx->source_count + 1,
            xremote_designer_assignment_changed,
            &ctx->item_contexts[i]);
        variable_item_set_current_value_index(item, source_index);
        variable_item_set_current_value_text(item, xremote_designer_source_name(ctx, source_index));
    }

    VariableItem* save_item =
        variable_item_list_add(ctx->item_list, "Save As...", 1, NULL, NULL);
    variable_item_set_current_value_text(save_item, "OK");
    variable_item_list_set_enter_callback(
        ctx->item_list, xremote_designer_map_enter_callback, ctx);
    variable_item_list_set_selected_item(ctx->item_list, 0);

    View* view = variable_item_list_get_view(ctx->item_list);
    view_set_previous_callback(view, xremote_designer_map_exit_callback);

    if(!ctx->map_view_added) {
        view_dispatcher_add_view(ctx->app_ctx->view_dispatcher, XRemoteViewDesignerMap, view);
        ctx->map_view_added = true;
    }

    view_dispatcher_switch_to_view(ctx->app_ctx->view_dispatcher, XRemoteViewDesignerMap);
}

static void xremote_designer_type_submenu_callback(void* context, uint32_t index) {
    XRemoteApp* app = context;
    xremote_app_assert_void(app);
    XRemoteDesignerContext* ctx = app->context;
    xremote_app_assert_void(ctx);

    if(index >= XRemoteRemoteTypeCount) index = XRemoteRemoteTypeGeneric;
    ctx->remote_type = index;
    xremote_designer_map_view_alloc(ctx);
}

static uint32_t xremote_designer_type_exit_callback(void* context) {
    UNUSED(context);
    return XRemoteViewSubmenu;
}

static XRemoteDesignerContext* xremote_designer_context_alloc(XRemoteAppContext* app_ctx) {
    XRemoteDesignerContext* ctx = malloc(sizeof(XRemoteDesignerContext));
    memset(ctx, 0, sizeof(XRemoteDesignerContext));

    ctx->app_ctx = app_ctx;
    ctx->source_remote = infrared_remote_alloc();
    ctx->output_remote = infrared_remote_alloc();
    ctx->item_list = variable_item_list_alloc();
    ctx->text_input = text_input_alloc();
    ctx->dialog_ex = dialog_ex_alloc();
    ctx->dialog_return_view = XRemoteViewDesignerMap;
    ctx->remote_type = XRemoteRemoteTypeGeneric;

    if(!infrared_remote_load(ctx->source_remote, app_ctx->file_path)) {
        xremote_designer_show_load_error("Cannot load\nsource .ir");
        goto fail;
    }

    const size_t button_count = infrared_remote_get_button_count(ctx->source_remote);
    if(button_count == 0) {
        xremote_designer_show_load_error("Source .ir has\nno commands");
        goto fail;
    }

    ctx->source_count = button_count > XREMOTE_DESIGNER_SOURCE_LIMIT ?
                            XREMOTE_DESIGNER_SOURCE_LIMIT :
                            button_count;

    View* view = text_input_get_view(ctx->text_input);
    view_set_previous_callback(view, xremote_designer_text_input_exit_callback);
    text_input_set_validator(ctx->text_input, NULL, ctx);
    view_dispatcher_add_view(app_ctx->view_dispatcher, XRemoteViewTextInput, view);

    view = dialog_ex_get_view(ctx->dialog_ex);
    view_dispatcher_add_view(app_ctx->view_dispatcher, XRemoteViewDialogExit, view);
    ctx->dialog_view_added = true;

    return ctx;

fail:
    if(ctx->dialog_ex) dialog_ex_free(ctx->dialog_ex);
    if(ctx->text_input) text_input_free(ctx->text_input);
    if(ctx->item_list) variable_item_list_free(ctx->item_list);
    if(ctx->output_remote) infrared_remote_free(ctx->output_remote);
    if(ctx->source_remote) infrared_remote_free(ctx->source_remote);
    free(ctx);
    return NULL;
}

static void xremote_designer_context_free(XRemoteDesignerContext* ctx) {
    xremote_app_assert_void(ctx);
    ViewDispatcher* view_disp = ctx->app_ctx->view_dispatcher;

    if(ctx->map_view_added) {
        view_dispatcher_remove_view(view_disp, XRemoteViewDesignerMap);
    }
    if(ctx->dialog_view_added) {
        view_dispatcher_remove_view(view_disp, XRemoteViewDialogExit);
    }
    view_dispatcher_remove_view(view_disp, XRemoteViewTextInput);

    dialog_ex_free(ctx->dialog_ex);
    text_input_free(ctx->text_input);
    variable_item_list_free(ctx->item_list);
    infrared_remote_free(ctx->output_remote);
    infrared_remote_free(ctx->source_remote);
    free(ctx);
}

static void xremote_designer_context_clear_callback(void* context) {
    xremote_designer_context_free(context);
}

XRemoteApp* xremote_designer_alloc(XRemoteAppContext* app_ctx) {
    if(!xremote_app_context_select_file(app_ctx, XREMOTE_APP_EXTENSION)) return NULL;

    XRemoteDesignerContext* designer_ctx = xremote_designer_context_alloc(app_ctx);
    if(!designer_ctx) return NULL;

    XRemoteApp* app = xremote_app_alloc(app_ctx);
    xremote_app_submenu_alloc(app, XRemoteViewDesigner, xremote_designer_type_exit_callback);
    xremote_app_set_user_context(app, designer_ctx, xremote_designer_context_clear_callback);

    for(uint32_t i = 0; i < XRemoteRemoteTypeCount; i++) {
        xremote_app_submenu_add(
            app,
            xremote_remote_type_get_menu_name(i),
            i,
            xremote_designer_type_submenu_callback);
    }

    return app;
}
