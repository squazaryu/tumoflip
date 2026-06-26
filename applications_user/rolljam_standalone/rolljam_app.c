// rolljam_app.c
#include "rolljam_app_i.h"
#undef TAG

#include <furi.h>
#include <furi_hal.h>
#include "protocols/protocol_items.h"
#include "protocols/protocols_common.h"
#include "helpers/rolljam_settings.h"
#include "helpers/rolljam_storage.h"
#include "helpers/rolljam_psa_bf_host.h"
#include "protocols/keys.h"
#include <string.h>

#define TAG "RollJamApp"
#undef TAG
#define TAG "RollJamApp"

#if defined(ENABLE_DUAL_RX_SCENE) || defined(ENABLE_SHIELD_RX_SCENE)
static bool rolljam_setting_has_frequency(SubGhzSetting* setting, uint32_t frequency) {
    size_t count = subghz_setting_get_frequency_count(setting);
    for(size_t i = 0; i < count; i++) {
        if(subghz_setting_get_frequency(setting, i) == frequency) {
            return true;
        }
    }
    return false;
}
#endif

#ifdef ENABLE_DUAL_RX_SCENE
static RollJamProtocolRegistryFilter rolljam_setting_preset_filter(
    SubGhzSetting* setting,
    uint8_t index) {
    return rolljam_get_protocol_registry_filter_for_preset(
        subghz_setting_get_preset_data(setting, index),
        subghz_setting_get_preset_data_size(setting, index));
}

static uint8_t rolljam_find_preset_by_name_or_filter(
    SubGhzSetting* setting,
    const char* preferred_name,
    RollJamProtocolRegistryFilter filter) {
    size_t count = subghz_setting_get_preset_count(setting);
    for(size_t i = 0; i < count; i++) {
        if(strcmp(subghz_setting_get_preset_name(setting, i), preferred_name) == 0 &&
           rolljam_setting_preset_filter(setting, (uint8_t)i) == filter) {
            return (uint8_t)i;
        }
    }
    for(size_t i = 0; i < count; i++) {
        if(rolljam_setting_preset_filter(setting, (uint8_t)i) == filter) {
            return (uint8_t)i;
        }
    }
    return UINT8_MAX;
}

static uint8_t
    rolljam_find_preset_by_name(SubGhzSetting* setting, const char* preset_name) {
    if(!preset_name || preset_name[0] == '\0') {
        return UINT8_MAX;
    }

    size_t count = subghz_setting_get_preset_count(setting);
    for(size_t i = 0; i < count; i++) {
        if(strcmp(subghz_setting_get_preset_name(setting, i), preset_name) == 0) {
            return (uint8_t)i;
        }
    }
    return UINT8_MAX;
}
#endif

static bool rolljam_app_custom_event_callback(void* context, uint32_t event) {
    furi_check(context);
    RollJamApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool rolljam_app_back_event_callback(void* context) {
    furi_check(context);
    RollJamApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void rolljam_app_tick_event_callback(void* context) {
    furi_check(context);
    RollJamApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

bool rolljam_ensure_variable_item_list(RollJamApp* app) {
    furi_check(app);
    if(app->variable_item_list) {
        return true;
    }

    app->variable_item_list = variable_item_list_alloc();
    if(!app->variable_item_list) {
        return false;
    }

    view_dispatcher_add_view(
        app->view_dispatcher,
        RollJamViewVariableItemList,
        variable_item_list_get_view(app->variable_item_list));
    return true;
}

bool rolljam_ensure_widget(RollJamApp* app) {
    furi_check(app);
    if(app->widget) {
        return true;
    }

    app->widget = widget_alloc();
    if(!app->widget) {
        return false;
    }

    view_dispatcher_add_view(
        app->view_dispatcher, RollJamViewWidget, widget_get_view(app->widget));
    return true;
}

bool rolljam_ensure_text_input(RollJamApp* app) {
    furi_check(app);
    if(app->text_input) {
        return true;
    }

    app->text_input = text_input_alloc();
    if(!app->text_input) {
        return false;
    }

    view_dispatcher_add_view(
        app->view_dispatcher, RollJamViewTextInput, text_input_get_view(app->text_input));
    return true;
}

bool rolljam_ensure_view_about(RollJamApp* app) {
    furi_check(app);
    if(app->view_about) {
        return true;
    }

    app->view_about = view_alloc();
    if(!app->view_about) {
        return false;
    }

    view_dispatcher_add_view(app->view_dispatcher, RollJamViewAbout, app->view_about);
    return true;
}

bool rolljam_ensure_receiver_view(RollJamApp* app) {
    furi_check(app);
    if(app->rolljam_receiver) {
        return true;
    }

    app->rolljam_receiver = rolljam_view_receiver_alloc(app->auto_save);
    if(!app->rolljam_receiver) {
        return false;
    }

    view_dispatcher_add_view(
        app->view_dispatcher,
        RollJamViewReceiver,
        rolljam_view_receiver_get_view(app->rolljam_receiver));
    return true;
}

#ifdef ENABLE_DUAL_RX_SCENE
bool rolljam_ensure_dual_receiver_view(RollJamApp* app) {
    furi_check(app);
    if(app->dual_receiver) {
        return true;
    }

    app->dual_receiver = rolljam_view_dual_receiver_alloc();
    if(!app->dual_receiver) {
        return false;
    }

    view_dispatcher_add_view(
        app->view_dispatcher,
        RollJamViewDualReceiver,
        rolljam_view_dual_receiver_get_view(app->dual_receiver));
    return true;
}
#endif

static void rolljam_radio_init_cleanup(RollJamApp* app, bool devices_initialized) {
    furi_check(app);
    furi_check(app->txrx);

    if(app->txrx->receiver) {
        subghz_receiver_free(app->txrx->receiver);
        app->txrx->receiver = NULL;
    }

    if(app->txrx->radio_device) {
        if(devices_initialized) {
            subghz_devices_idle(app->txrx->radio_device);
        }
        radio_device_loader_end(app->txrx->radio_device);
        app->txrx->radio_device = NULL;
    }

    if(app->txrx->environment) {
        subghz_environment_free(app->txrx->environment);
        app->txrx->environment = NULL;
    }

    if(app->txrx->protocol_plugin_manager) {
        rolljam_unload_protocol_plugin(app->txrx);
    }

    if(app->txrx->plugin_resolver) {
        composite_api_resolver_free(app->txrx->plugin_resolver);
        app->txrx->plugin_resolver = NULL;
    }

    if(devices_initialized) {
        subghz_devices_deinit();
    }

    app->txrx->protocol_registry = NULL;
    app->txrx->protocol_plugin = NULL;
    app->txrx->protocol_registry_filter = RollJamProtocolRegistryFilterAM;
    app->txrx->txrx_state = RollJamTxRxStateIDLE;
    app->radio_initialized = false;
}

RollJamApp* rolljam_app_alloc() {
    rolljam_storage_purge_temp_history_at_startup();
    RollJamApp* app = malloc(sizeof(RollJamApp));
    if(!app) {
        FURI_LOG_E(TAG, "Failed to allocate RollJamApp app !");
        return NULL;
    }
    memset(app, 0, sizeof(RollJamApp));

    FURI_LOG_I(TAG, "Allocating RollJam Decoder App");

    // GUI
    app->gui = furi_record_open(RECORD_GUI);

    // View Dispatcher
    app->view_dispatcher = view_dispatcher_alloc();
#if defined(FW_ORIGIN_RM)
    view_dispatcher_enable_queue(app->view_dispatcher);
#endif
    app->scene_manager = scene_manager_alloc(&rolljam_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, rolljam_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, rolljam_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, rolljam_app_tick_event_callback, 100);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Open Notification record
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    // Open Dialogs record
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    // SubMenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RollJamViewSubmenu, submenu_get_view(app->submenu));

    app->save_protocol = NULL;
    app->save_from_saved_info = false;
    app->save_history_idx = 0;
    app->emulate_disabled_for_loaded = false;
    memset(app->save_filename, 0, sizeof(app->save_filename));

    // File Browser path
    app->file_path = furi_string_alloc();
    furi_string_set(app->file_path, ROLLJAM_APP_FOLDER);

    // Load saved settings
    RollJamSettings settings;
    rolljam_settings_load(&settings);

    // Apply auto-save setting
    app->auto_save = settings.auto_save;
    app->tx_power = settings.tx_power;
    if(app->tx_power >= 9U) {
        app->tx_power = 0;
    }
    app->emulate_feature_enabled = settings.emulate_feature_enabled;

    // Init setting - KEEP THIS, it's small
    app->setting = subghz_setting_alloc();
    app->loaded_file_path = NULL;
    app->start_tx_time = 0;
    subghz_setting_load(app->setting, EXT_PATH("subghz/assets/setting_user"));

    // Apply loaded frequency and preset, with validation
    uint32_t frequency = settings.frequency;
    uint8_t preset_index = settings.preset_index;

    // Validate frequency
    bool frequency_valid = false;
    for(size_t i = 0; i < subghz_setting_get_frequency_count(app->setting); i++) {
        if(subghz_setting_get_frequency(app->setting, i) == frequency) {
            frequency_valid = true;
            break;
        }
    }
    if(!frequency_valid) {
        frequency = subghz_setting_get_default_frequency(app->setting);
        FURI_LOG_W(TAG, "Saved frequency invalid, using default: %lu", frequency);
    }

    // Validate preset index
    if(preset_index >= subghz_setting_get_preset_count(app->setting)) {
        preset_index = 0;
        FURI_LOG_W(TAG, "Saved preset index invalid, using default");
    }

    // Initialize TxRx structure with minimal setup
    app->lock = RollJamLockOff;
    app->txrx = malloc(sizeof(RollJamTxRx));
    furi_check(app->txrx);
    memset(app->txrx, 0, sizeof(RollJamTxRx));

    app->txrx->preset = malloc(sizeof(SubGhzRadioPreset));
    furi_check(app->txrx->preset);
    app->txrx->preset->name = furi_string_alloc();
    furi_check(app->txrx->preset->name);
    app->txrx->txrx_state = RollJamTxRxStateIDLE;
    app->txrx->rx_key_state = RollJamRxKeyStateIDLE;
    app->txrx->protocol_registry_filter = RollJamProtocolRegistryFilterAM;

    // Get preset name and data
    const char* preset_name = subghz_setting_get_preset_name(app->setting, preset_index);
    uint8_t* preset_data = subghz_setting_get_preset_data(app->setting, preset_index);
    size_t preset_data_size = subghz_setting_get_preset_data_size(app->setting, preset_index);

    FURI_LOG_I(
        TAG,
        "Settings: freq=%lu, preset=%s, auto_save=%d, hopping=%d",
        frequency,
        preset_name,
        settings.auto_save,
        settings.hopping_enabled);

    rolljam_preset_init(app, preset_name, frequency, preset_data, preset_data_size);

#ifdef ENABLE_DUAL_RX_SCENE

    uint32_t default_frequency = subghz_setting_get_default_frequency(app->setting);
    app->dual_freq_a = rolljam_setting_has_frequency(app->setting, settings.dual_freq_a) ?
                           settings.dual_freq_a :
                           default_frequency;
    app->dual_freq_b = rolljam_setting_has_frequency(app->setting, settings.dual_freq_b) ?
                           settings.dual_freq_b :
                           default_frequency;
    uint8_t preset_count = (uint8_t)subghz_setting_get_preset_count(app->setting);
    uint8_t named_preset_a =
        rolljam_find_preset_by_name(app->setting, settings.dual_preset_name_a);
    uint8_t named_preset_b =
        rolljam_find_preset_by_name(app->setting, settings.dual_preset_name_b);
    app->dual_preset_a =
        named_preset_a != UINT8_MAX ? named_preset_a : settings.dual_preset_a;
    app->dual_preset_b =
        named_preset_b != UINT8_MAX ? named_preset_b : settings.dual_preset_b;
    if(preset_count == 0) {
        app->dual_preset_a = UINT8_MAX;
        app->dual_preset_b = UINT8_MAX;
    } else if(app->dual_preset_a >= preset_count) {
        app->dual_preset_a = rolljam_find_preset_by_name_or_filter(
            app->setting, "AM650", RollJamProtocolRegistryFilterAM);
        if(app->dual_preset_a == UINT8_MAX) {
            app->dual_preset_a = 0;
        }
    }
    if(app->dual_preset_b >= preset_count) {
        app->dual_preset_b = rolljam_find_preset_by_name_or_filter(
            app->setting, "FM476", RollJamProtocolRegistryFilterFM);
        if(app->dual_preset_b == UINT8_MAX) {
            app->dual_preset_b = 0;
        }
    }
#endif

#ifdef ENABLE_SHIELD_RX_SCENE
    {
        uint32_t default_frequency = subghz_setting_get_default_frequency(app->setting);
        app->shield_freq = rolljam_setting_has_frequency(app->setting, settings.shield_freq) ?
                               settings.shield_freq :
                               default_frequency;
        app->shield_preset_index = settings.shield_preset_index;
        if(app->shield_preset_index >= subghz_setting_get_preset_count(app->setting)) {
            app->shield_preset_index = preset_index;
        }
        app->shield_tx_offset_index = settings.shield_tx_offset_index;
        if(app->shield_tx_offset_index >= 12U) {
            app->shield_tx_offset_index = 3U;
        }
        app->shield_tx_power = settings.shield_tx_power;
        if(app->shield_tx_power >= 9U) {
            app->shield_tx_power = 0U;
        }
    }
#endif

    // Apply hopping state from settings
    app->txrx->hopper_state = settings.hopping_enabled ? RollJamHopperStateRunning :
                                                         RollJamHopperStateOFF;
    app->txrx->hopper_idx_frequency = 0;
    app->txrx->hopper_timeout = 0;
    app->txrx->idx_menu_chosen = 0;

    app->radio_initialized = false;

    return app;
}

bool rolljam_radio_init(RollJamApp* app) {
    furi_check(app);
    furi_check(app->txrx);

    FURI_LOG_I(TAG, "=== rolljam_radio_init called ===");
    FURI_LOG_D(TAG, "State: radio_initialized=%d", app->radio_initialized);

    if(app->radio_initialized) {
        const bool radio_ready = (app->txrx->environment != NULL) &&
                                 (app->txrx->radio_device != NULL);
        if(radio_ready) {
            FURI_LOG_D(TAG, "Radio already initialized, returning true");
            return true;
        }

        FURI_LOG_W(
            TAG,
            "Radio marked initialized but resources missing (env=%p device=%p), repairing",
            app->txrx->environment,
            app->txrx->radio_device);
        rolljam_radio_deinit(app);
    }

    // Fresh radio init - nothing was initialized before
    FURI_LOG_I(TAG, "Fresh radio init - allocating all components");

    // Create environment with our custom protocols
    app->txrx->environment = subghz_environment_alloc();
    if(!app->txrx->environment) {
        FURI_LOG_E(TAG, "Failed to allocate environment!");
        rolljam_radio_init_cleanup(app, false);
        return false;
    }

    app->txrx->protocol_registry = NULL;

    if(!rolljam_refresh_protocol_registry(app, false)) {
        FURI_LOG_E(TAG, "Failed to configure protocol registry");
        rolljam_radio_init_cleanup(app, false);
        return false;
    }

    // Load keystores
    subghz_environment_load_keystore(app->txrx->environment, ROLLJAM_KEYSTORE_DIR_NAME);

    // Load RollJam specific keys
    rolljam_keys_load(app->txrx->environment);
    FURI_LOG_I(TAG, "Loaded RollJam secure keys");

    // Initialize SubGhz devices
    subghz_devices_init();
    FURI_LOG_D(TAG, "SubGhz devices initialized");

    // Try external CC1101 first
    app->txrx->radio_device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeExternalCC1101);

    // if not loading, fallback to internal
    if(!app->txrx->radio_device) {
        FURI_LOG_W(TAG, "External CC1101 not found, trying internal radio");
        app->txrx->radio_device = radio_device_loader_set(NULL, SubGhzRadioDeviceTypeInternal);
    }

    if(!app->txrx->radio_device) {
        FURI_LOG_E(TAG, "Failed to initialize any radio device!");
        rolljam_radio_init_cleanup(app, true);
        return false;
    }
#ifndef REMOVE_LOGS
    const char* device_name = subghz_devices_get_name(app->txrx->radio_device);
    bool is_external = device_name && strstr(device_name, "ext");
    FURI_LOG_I(
        TAG,
        "Radio device initialized: %s (%s)",
        device_name ? device_name : "unknown",
        is_external ? "external" : "internal");
#endif
    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);

    app->radio_initialized = true;

    FURI_LOG_D(TAG, "Final state: radio_initialized=%d", app->radio_initialized);

    return true;
}

// Deinitialize radio subsystem
void rolljam_radio_deinit(RollJamApp* app) {
    FURI_LOG_I(TAG, "=== rolljam_radio_deinit called ===");
    FURI_LOG_D(TAG, "State: radio_initialized=%d", app->radio_initialized);
    FURI_LOG_D(
        TAG,
        "Pointers: worker=%p, environment=%p, receiver=%p, history=%p, radio_device=%p",
        app->txrx->worker,
        app->txrx->environment,
        app->txrx->receiver,
        app->txrx->history,
        app->txrx->radio_device);

    bool has_radio_resources = app->radio_initialized || app->txrx->worker ||
                               app->txrx->environment || app->txrx->receiver ||
                               app->txrx->history || app->txrx->radio_device;
    if(!has_radio_resources) {
        FURI_LOG_D(TAG, "Radio resources were not initialized, returning");
        return;
    }

    bool devices_initialized = app->radio_initialized || (app->txrx->radio_device != NULL);

    // Make sure we're not receiving
    if(app->txrx->worker && app->txrx->txrx_state == RollJamTxRxStateRx) {
        FURI_LOG_D(TAG, "Stopping active RX, state=%d", app->txrx->txrx_state);
        subghz_worker_stop(app->txrx->worker);
        if(app->txrx->radio_device) {
            subghz_devices_stop_async_rx(app->txrx->radio_device);
        }
    }

    if(app->txrx->radio_device) {
        FURI_LOG_D(TAG, "Putting radio device to sleep and ending: %p", app->txrx->radio_device);
        subghz_devices_sleep(app->txrx->radio_device);
        radio_device_loader_end(app->txrx->radio_device);
        app->txrx->radio_device = NULL;
    } else {
        FURI_LOG_D(TAG, "Radio device was NULL, skipping sleep/end");
    }

    if(devices_initialized) {
        FURI_LOG_D(TAG, "Calling subghz_devices_deinit");
        subghz_devices_deinit();
    }

    if(app->txrx->receiver) {
        FURI_LOG_D(TAG, "Freeing receiver %p", app->txrx->receiver);
        subghz_receiver_free(app->txrx->receiver);
        app->txrx->receiver = NULL;
    } else {
        FURI_LOG_D(TAG, "Receiver was NULL, skipping free");
    }

    if(app->txrx->environment) {
        FURI_LOG_D(TAG, "Freeing environment %p", app->txrx->environment);
        subghz_environment_free(app->txrx->environment);
        app->txrx->environment = NULL;
        app->txrx->protocol_registry = NULL;
    } else {
        FURI_LOG_D(TAG, "Environment was NULL, skipping free");
    }

    if(app->txrx->protocol_plugin_manager) {
        rolljam_unload_protocol_plugin(app->txrx);
    }


    if(app->txrx->plugin_resolver) {
        FURI_LOG_D(TAG, "Freeing plugin resolver %p", app->txrx->plugin_resolver);
        composite_api_resolver_free(app->txrx->plugin_resolver);
        app->txrx->plugin_resolver = NULL;
    }
    app->txrx->protocol_plugin = NULL;

    if(app->txrx->history) {
        FURI_LOG_D(TAG, "Freeing history %p", app->txrx->history);
        if(app->selected_capture.history == app->txrx->history) {
            rolljam_selected_capture_clear(app);
        }
        rolljam_history_free(app->txrx->history);
        app->txrx->history = NULL;
    } else {
        FURI_LOG_D(TAG, "History was NULL, skipping free");
    }

    if(app->txrx->worker) {
        FURI_LOG_D(TAG, "Freeing worker %p", app->txrx->worker);
        subghz_worker_free(app->txrx->worker);
        app->txrx->worker = NULL;
    } else {
        FURI_LOG_D(TAG, "Worker was NULL, skipping free");
    }

    app->txrx->txrx_state = RollJamTxRxStateIDLE;
    app->radio_initialized = false;

    FURI_LOG_D(TAG, "Final state: radio_initialized=%d", app->radio_initialized);
}

void rolljam_app_free(RollJamApp* app) {
    furi_check(app);

    FURI_LOG_I(TAG, "=== rolljam_app_free called ===");
    FURI_LOG_D(TAG, "State: radio_initialized=%d", app->radio_initialized);

    // Save settings before exiting
    RollJamSettings settings;
    rolljam_settings_load(&settings);
    settings.frequency = app->txrx->preset->frequency;
    settings.auto_save = app->auto_save;
    settings.tx_power = app->tx_power;
    settings.hopping_enabled = (app->txrx->hopper_state != RollJamHopperStateOFF);
    settings.emulate_feature_enabled = app->emulate_feature_enabled;
#ifdef ENABLE_DUAL_RX_SCENE
    settings.dual_freq_a = app->dual_freq_a;
    settings.dual_freq_b = app->dual_freq_b;
    settings.dual_preset_a = app->dual_preset_a;
    settings.dual_preset_b = app->dual_preset_b;
    settings.dual_preset_name_a[0] = '\0';
    settings.dual_preset_name_b[0] = '\0';
    size_t dual_preset_count = subghz_setting_get_preset_count(app->setting);
    if(app->dual_preset_a < dual_preset_count) {
        snprintf(
            settings.dual_preset_name_a,
            sizeof(settings.dual_preset_name_a),
            "%s",
            subghz_setting_get_preset_name(app->setting, app->dual_preset_a));
    }
    if(app->dual_preset_b < dual_preset_count) {
        snprintf(
            settings.dual_preset_name_b,
            sizeof(settings.dual_preset_name_b),
            "%s",
            subghz_setting_get_preset_name(app->setting, app->dual_preset_b));
    }
#endif
#ifdef ENABLE_SHIELD_RX_SCENE
    settings.shield_freq = app->shield_freq;
    settings.shield_preset_index = app->shield_preset_index;
    settings.shield_tx_offset_index = app->shield_tx_offset_index;
    settings.shield_tx_power = app->shield_tx_power;
#endif

    // Find current preset index
    settings.preset_index = 0;
    const char* current_preset = furi_string_get_cstr(app->txrx->preset->name);
    for(uint8_t i = 0; i < subghz_setting_get_preset_count(app->setting); i++) {
        if(strcmp(subghz_setting_get_preset_name(app->setting, i), current_preset) == 0) {
            settings.preset_index = i;
            break;
        }
    }

    FURI_LOG_I(
        TAG,
        "Saving settings: freq=%lu, preset=%u, auto_save=%d, hopping=%d, emulate=%d",
        settings.frequency,
        settings.preset_index,
        settings.auto_save,
        settings.hopping_enabled,
        settings.emulate_feature_enabled);

    rolljam_settings_save(&settings);

    // Deinitialize whichever is active - NULL checks inside handle all cases
    FURI_LOG_D(TAG, "Calling radio_deinit");
    rolljam_radio_deinit(app);

    if(app->loaded_file_path) {
        FURI_LOG_D(TAG, "Freeing loaded_file_path");
        furi_string_free(app->loaded_file_path);
        app->loaded_file_path = NULL;
    }

    // Submenu
    if(app->submenu) {
        FURI_LOG_D(TAG, "Removing submenu view");
        view_dispatcher_remove_view(app->view_dispatcher, RollJamViewSubmenu);
        submenu_free(app->submenu);
    }

    // Variable Item List
    if(app->variable_item_list) {
        FURI_LOG_D(TAG, "Removing variable_item_list view");
        view_dispatcher_remove_view(app->view_dispatcher, RollJamViewVariableItemList);
        variable_item_list_free(app->variable_item_list);
    }

    // About View
    if(app->view_about) {
        FURI_LOG_D(TAG, "Removing about view");
        view_dispatcher_remove_view(app->view_dispatcher, RollJamViewAbout);
        view_free(app->view_about);
    }

    // File path
    if(app->file_path) {
        FURI_LOG_D(TAG, "Freeing file_path");
        furi_string_free(app->file_path);
    }

    // Widget
    if(app->widget) {
        FURI_LOG_D(TAG, "Removing widget view");
        view_dispatcher_remove_view(app->view_dispatcher, RollJamViewWidget);
        widget_free(app->widget);
    }

    // Text Input
    if(app->text_input) {
        FURI_LOG_D(TAG, "Removing text_input view");
        view_dispatcher_remove_view(app->view_dispatcher, RollJamViewTextInput);
        text_input_free(app->text_input);
    }
    if(app->save_protocol) {
        furi_string_free(app->save_protocol);
        app->save_protocol = NULL;
    }

    // Receiver
    if(app->rolljam_receiver) {
        FURI_LOG_D(TAG, "Removing receiver view");
        view_dispatcher_remove_view(app->view_dispatcher, RollJamViewReceiver);
        rolljam_view_receiver_free(app->rolljam_receiver);
    }

#ifdef ENABLE_DUAL_RX_SCENE

    bool dual_devices_initialized = app->dual_chain_a || app->dual_chain_b;
    if(app->dual_chain_a) {
        rolljam_rx_chain_free(app->dual_chain_a);
        app->dual_chain_a = NULL;
    }
    if(app->dual_chain_b) {
        rolljam_rx_chain_free(app->dual_chain_b);
        app->dual_chain_b = NULL;
    }
    if(dual_devices_initialized) {
        subghz_devices_deinit();
    }
    if(app->dual_receiver) {
        FURI_LOG_D(TAG, "Removing dual receiver view");
        view_dispatcher_remove_view(app->view_dispatcher, RollJamViewDualReceiver);
        rolljam_view_dual_receiver_free(app->dual_receiver);
        app->dual_receiver = NULL;
    }
    if(app->dual_history) {
        if(app->selected_capture.history == app->dual_history) {
            rolljam_selected_capture_clear(app);
        }
        rolljam_history_free(app->dual_history);
        app->dual_history = NULL;
    }
    if(app->dual_history_mutex) {
        furi_mutex_free(app->dual_history_mutex);
        app->dual_history_mutex = NULL;
    }
#endif

#ifdef ENABLE_SHIELD_RX_SCENE
    bool shield_devices_initialized = app->shield_rx_chain || app->shield_tx_chain;
    if(app->shield_rx_chain) {
        rolljam_rx_chain_free(app->shield_rx_chain);
        app->shield_rx_chain = NULL;
    }
    if(app->shield_tx_chain) {
        rolljam_tx_chain_free(app->shield_tx_chain);
        app->shield_tx_chain = NULL;
    }
    if(shield_devices_initialized) {
        subghz_devices_deinit();
    }
    if(app->shield_history) {
        if(app->selected_capture.history == app->shield_history) {
            rolljam_selected_capture_clear(app);
        }
        rolljam_history_free(app->shield_history);
        app->shield_history = NULL;
    }
    if(app->shield_history_mutex) {
        furi_mutex_free(app->shield_history_mutex);
        app->shield_history_mutex = NULL;
    }
#endif

    // rolljam_psa_bf_context_release(app);

#ifdef ENABLE_SHIELD_RX_SCENE
    // rolljam_scene_shield_receiver_cleanup_rolljam_tx();
#endif

    // Setting
    FURI_LOG_D(TAG, "Freeing subghz_setting");
    subghz_setting_free(app->setting);

    // Free preset
    FURI_LOG_D(TAG, "Freeing preset");
    furi_string_free(app->txrx->preset->name);
    free(app->txrx->preset);

    free(app->txrx);

#ifdef ENABLE_EMULATE_FEATURE
    rolljam_emulate_context_release(app);
#endif

    pp_shared_upload_release();

    // View dispatcher
    FURI_LOG_D(TAG, "Freeing view_dispatcher and scene_manager");
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    // Close Dialogs
    FURI_LOG_D(TAG, "Closing dialogs record");
    furi_record_close(RECORD_DIALOGS);
    app->dialogs = NULL;

    // Notifications
    FURI_LOG_D(TAG, "Closing notifications record");
    furi_record_close(RECORD_NOTIFICATION);
    app->notifications = NULL;

    // Close records
    FURI_LOG_D(TAG, "Closing GUI record");
    furi_record_close(RECORD_GUI);

    FURI_LOG_I(TAG, "App free complete");
    free(app);
}

int32_t rolljam_app(char* p) {
    UNUSED(p);
    FURI_LOG_I("RollJam", "Starting RollJam App...");
    furi_hal_power_suppress_charge_enter();

    RollJamApp* rolljam_app = rolljam_app_alloc();
    if(!rolljam_app) {
        FURI_LOG_E("RollJam", "Failed to allocate RollJamApp");
        furi_hal_power_suppress_charge_exit();
        return -1;
    }
    FURI_LOG_I("RollJam", "App allocated successfully");

    scene_manager_next_scene(
        rolljam_app->scene_manager,
        RollJamSceneStart);
    FURI_LOG_I("RollJam", "Switched to Start Scene");

    view_dispatcher_run(rolljam_app->view_dispatcher);
    FURI_LOG_I("RollJam", "View dispatcher finished");

    rolljam_app_free(rolljam_app);

    furi_hal_power_suppress_charge_exit();

    return 0;
}
