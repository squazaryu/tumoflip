#include <subghz/subghz_i.h>
#include <subghz/helpers/subghz_feature_plugin.h>
#include <toolbox/path.h>
#include "workspace_model.h"

#define WORKSPACE_DIR  EXT_PATH("subghz/profiles")
#define WORKSPACE_TYPE "Tumoflip SubGhz Work Profile"

typedef enum {
    WorkspaceMenu,
    WorkspaceName,
    WorkspacePreview,
    WorkspaceResult
} WorkspacePage;
typedef enum {
    WorkspaceSave = 10,
    WorkspaceLoad,
    WorkspaceNamed,
    WorkspaceApply
} WorkspaceEvent;
typedef struct {
    SubGhz* app;
    WorkspacePage page;
    WorkspaceProfile profile;
    char name[WORKSPACE_NAME_SIZE];
} Workspace;

static void workspace_event(void* context, uint32_t event) {
    Workspace* w = context;
    view_dispatcher_send_custom_event(w->app->view_dispatcher, event);
}

static void workspace_named(void* context) {
    workspace_event(context, WorkspaceNamed);
}

static void workspace_button(GuiButtonType button, InputType type, void* context) {
    if(button == GuiButtonTypeRight && type == InputTypeShort)
        workspace_event(context, WorkspaceApply);
}

static void workspace_menu(Workspace* w) {
    w->page = WorkspaceMenu;
    text_input_reset(w->app->text_input);
    widget_reset(w->app->widget);
    submenu_reset(w->app->submenu);
    submenu_set_header(w->app->submenu, "Work Profiles");
    submenu_add_item(w->app->submenu, "Save current as...", WorkspaceSave, workspace_event, w);
    submenu_add_item(w->app->submenu, "Load profile", WorkspaceLoad, workspace_event, w);
    view_dispatcher_switch_to_view(w->app->view_dispatcher, SubGhzViewIdMenu);
}

static void workspace_message(Workspace* w, const char* text) {
    w->page = WorkspaceResult;
    widget_reset(w->app->widget);
    widget_add_text_box_element(w->app->widget, 0, 2, 128, 60, AlignLeft, AlignTop, text, false);
    view_dispatcher_switch_to_view(w->app->view_dispatcher, SubGhzViewIdWidget);
}

static bool workspace_capture(Workspace* w) {
    const SubGhzLastSettings* s = w->app->last_settings;
    SubGhzSetting* setting = subghz_txrx_get_setting(w->app->txrx);
    if(s->preset_index >= subghz_setting_get_preset_count(setting) ||
       s->raw_preset_index >= subghz_setting_get_preset_count(setting))
        return false;
    w->profile = (WorkspaceProfile){
        .frequency = s->frequency,
        .raw_frequency = s->raw_frequency,
        .pack = s->protocol_pack_group,
        .hopping = s->hopping_mode,
        .radio = subghz_txrx_radio_device_get(w->app->txrx)};
    if(strlcpy(
           w->profile.preset,
           subghz_setting_get_preset_name(setting, s->preset_index),
           sizeof(w->profile.preset)) >= sizeof(w->profile.preset) ||
       strlcpy(
           w->profile.raw_preset,
           subghz_setting_get_preset_name(setting, s->raw_preset_index),
           sizeof(w->profile.raw_preset)) >= sizeof(w->profile.raw_preset))
        return false;
    return workspace_profile_valid(&w->profile, SubGhzProtocolPackGroupCount);
}

static bool workspace_save(Workspace* w) {
    if(!workspace_name_valid(w->name) || !workspace_capture(w)) return false;
    Storage* storage = w->app->storage;
    FS_Error mkdir_error = storage_common_mkdir(storage, WORKSPACE_DIR);
    if(mkdir_error != FSE_OK && mkdir_error != FSE_EXIST) return false;
    FuriString* path = furi_string_alloc_printf(WORKSPACE_DIR "/%s.subprofile", w->name);
    FuriString* data = furi_string_alloc_printf(
        "Filetype: " WORKSPACE_TYPE "\nVersion: 1\nFrequency: %lu\nRawFrequency: %lu\n"
        "Preset: %s\nRawPreset: %s\nPack: %lu\nHopping: %lu\nRadio: %lu\n",
        w->profile.frequency,
        w->profile.raw_frequency,
        w->profile.preset,
        w->profile.raw_preset,
        w->profile.pack,
        w->profile.hopping,
        w->profile.radio);
    File* file = storage_file_alloc(storage);
    bool created =
        storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
    bool ok = created &&
              storage_file_write(file, furi_string_get_cstr(data), furi_string_size(data)) ==
                  furi_string_size(data) &&
              storage_file_sync(file);
    if(created) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(created && !ok) storage_common_remove(storage, furi_string_get_cstr(path));
    furi_string_free(data);
    furi_string_free(path);
    return ok;
}

static bool workspace_read(Workspace* w, const char* path) {
    FileInfo info;
    if(storage_common_stat(w->app->storage, path, &info) != FSE_OK || info.size > 1024)
        return false;
    FlipperFormat* file = flipper_format_file_alloc(w->app->storage);
    FuriString* text = furi_string_alloc();
    uint32_t version;
    WorkspaceProfile p = {0};
    flipper_format_set_strict_mode(file, true);
    bool ok = flipper_format_file_open_existing(file, path) &&
              flipper_format_read_header(file, text, &version) && version == 1 &&
              !strcmp(furi_string_get_cstr(text), WORKSPACE_TYPE) &&
              flipper_format_read_uint32(file, "Frequency", &p.frequency, 1) &&
              flipper_format_read_uint32(file, "RawFrequency", &p.raw_frequency, 1) &&
              flipper_format_read_string(file, "Preset", text) &&
              strlcpy(p.preset, furi_string_get_cstr(text), sizeof(p.preset)) < sizeof(p.preset) &&
              flipper_format_read_string(file, "RawPreset", text) &&
              strlcpy(p.raw_preset, furi_string_get_cstr(text), sizeof(p.raw_preset)) <
                  sizeof(p.raw_preset) &&
              flipper_format_read_uint32(file, "Pack", &p.pack, 1) &&
              flipper_format_read_uint32(file, "Hopping", &p.hopping, 1) &&
              flipper_format_read_uint32(file, "Radio", &p.radio, 1) &&
              workspace_profile_valid(&p, SubGhzProtocolPackGroupCount);
    flipper_format_free(file);
    furi_string_free(text);
    if(ok) w->profile = p;
    return ok;
}

static void workspace_preview(Workspace* w) {
    FuriString* text = furi_string_alloc_printf(
        "Read: %lu.%03lu %s\nRAW: %lu.%03lu %s\n%s / %s\nRadio: %s",
        w->profile.frequency / 1000000,
        (w->profile.frequency % 1000000) / 1000,
        w->profile.preset,
        w->profile.raw_frequency / 1000000,
        (w->profile.raw_frequency % 1000000) / 1000,
        w->profile.raw_preset,
        subghz_protocol_pack_group_get_name(w->profile.pack),
        (const char*[]){"Hop off", "Frequency", "Preset", "Combined"}[w->profile.hopping],
        w->profile.radio == SubGhzRadioDeviceTypeInternal ? "Internal" :
        w->profile.radio == SubGhzRadioDeviceTypeAuto     ? "Dual" :
                                                            "External");
    widget_reset(w->app->widget);
    widget_add_text_scroll_element(w->app->widget, 0, 0, 128, 48, furi_string_get_cstr(text));
    widget_add_button_element(w->app->widget, GuiButtonTypeRight, "Apply", workspace_button, w);
    furi_string_free(text);
    w->page = WorkspacePreview;
    view_dispatcher_switch_to_view(w->app->view_dispatcher, SubGhzViewIdWidget);
}

static void workspace_load(Workspace* w) {
    FuriString* path = furi_string_alloc_set(WORKSPACE_DIR);
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, ".subprofile", NULL);
    options.base_path = WORKSPACE_DIR;
    if(dialog_file_browser_show(w->app->dialogs, path, path, &options)) {
        if(workspace_read(w, furi_string_get_cstr(path))) {
            FuriString* name = furi_string_alloc();
            path_extract_filename(path, name, true);
            strlcpy(w->name, furi_string_get_cstr(name), sizeof(w->name));
            furi_string_free(name);
            workspace_preview(w);
        } else
            workspace_message(w, "Invalid profile.\nNothing changed.\nBack to return.");
    }
    furi_string_free(path);
}

static void workspace_apply(Workspace* w) {
    SubGhz* app = w->app;
    SubGhzSetting* setting = subghz_txrx_get_setting(app->txrx);
    int preset = subghz_setting_get_inx_preset_by_name(setting, w->profile.preset);
    int raw_preset = subghz_setting_get_inx_preset_by_name(setting, w->profile.raw_preset);
    if(preset < 0 || raw_preset < 0) {
        workspace_message(w, "Preset unavailable.\nNothing changed.\nBack to return.");
        return;
    }
    SubGhzRadioDeviceType old_radio = subghz_txrx_radio_device_get(app->txrx);
    SubGhzProtocolPackGroup old_pack = subghz_txrx_get_protocol_pack_group(app->txrx);
    const char* error = "Radio unavailable.\nNothing applied.";
    bool ok = subghz_txrx_radio_device_set(app->txrx, w->profile.radio) == w->profile.radio &&
              subghz_txrx_radio_device_is_frequency_valid(app->txrx, w->profile.frequency) &&
              subghz_txrx_radio_device_is_frequency_valid(app->txrx, w->profile.raw_frequency);
    if(ok) {
        ok = subghz_txrx_reload_protocol_pack(app->txrx, w->profile.pack);
        const SubGhzProtocolPackReport* report = subghz_txrx_get_protocol_pack_report(app->txrx);
        ok = ok && report && report->loaded_plugin_count == report->expected_plugin_count;
        error = "Protocol Pack incomplete.\nNothing applied.";
    }
    if(ok) {
        app->last_settings->frequency = w->profile.frequency;
        app->last_settings->raw_frequency = w->profile.raw_frequency;
        app->last_settings->preset_index = preset;
        app->last_settings->raw_preset_index = raw_preset;
        app->last_settings->protocol_pack_group = w->profile.pack;
        app->last_settings->hopping_mode = w->profile.hopping;
    }
    if(!ok) {
        bool restored = subghz_txrx_reload_protocol_pack(app->txrx, old_pack);
        const SubGhzProtocolPackReport* restored_report =
            subghz_txrx_get_protocol_pack_report(app->txrx);
        restored = restored && restored_report &&
                   restored_report->loaded_plugin_count == restored_report->expected_plugin_count;
        restored = subghz_txrx_radio_device_set(app->txrx, old_radio) == old_radio && restored;
        workspace_message(
            w, restored ? error : "Restore failed.\nRadio may have changed.\nReopen Sub-GHz.");
    } else
        workspace_message(
            w,
            "Loaded for this session.\nRead / RAW are separate.\nStartup defaults unchanged.\nBack to return.");
}

static void* workspace_alloc(SubGhz* app) {
    Workspace* w = calloc(1, sizeof(*w));
    w->app = app;
    workspace_menu(w);
    return w;
}

static bool workspace_on_event(void* context, SceneManagerEvent event) {
    Workspace* w = context;
    if(event.type == SceneManagerEventTypeBack) {
        if(w->page == WorkspaceMenu) return false;
        workspace_menu(w);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(w->page == WorkspaceMenu && event.event == WorkspaceSave) {
        w->page = WorkspaceName;
        w->name[0] = 0;
        text_input_reset(w->app->text_input);
        text_input_set_header_text(w->app->text_input, "New profile name");
        text_input_set_result_callback(
            w->app->text_input, workspace_named, w, w->name, sizeof(w->name), true);
        view_dispatcher_switch_to_view(w->app->view_dispatcher, SubGhzViewIdTextInput);
    } else if(w->page == WorkspaceMenu && event.event == WorkspaceLoad)
        workspace_load(w);
    else if(w->page == WorkspaceName && event.event == WorkspaceNamed) {
        workspace_message(
            w,
            workspace_save(w) ?
                "Profile saved.\nBack to return." :
                "Cannot save. Use a new\nname: letters, digits,\nspaces, - or _.\nCheck SD / presets.");
    } else if(w->page == WorkspacePreview && event.event == WorkspaceApply)
        workspace_apply(w);
    return true;
}

static void workspace_free(void* context) {
    Workspace* w = context;
    text_input_reset(w->app->text_input);
    submenu_reset(w->app->submenu);
    widget_reset(w->app->widget);
    free(w);
}

static const SubGhzWorkspacePlugin api = {workspace_alloc, workspace_on_event, workspace_free};
static const FlipperAppPluginDescriptor descriptor = {
    .appid = SUBGHZ_WORKSPACE_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_FEATURE_PLUGIN_API_VERSION,
    .entry_point = &api,
};
const FlipperAppPluginDescriptor* subghz_workspaces_ep(void) {
    return &descriptor;
}
