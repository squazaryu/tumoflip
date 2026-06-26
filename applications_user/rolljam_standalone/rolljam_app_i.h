// rolljam_app_i.h
#pragma once

#include <stddef.h>
#include "helpers/rolljam_types.h"
#include "helpers/rolljam_settings.h"
#include "scenes/rolljam_scene.h"
#include "views/rolljam_receiver.h"
#include "rolljam_history.h"
#include "helpers/rolljam_selected_capture.h"
#include "helpers/rolljam_rx_chain.h"
#include "helpers/rolljam_tx_chain.h"
#include "helpers/radio_device_loader.h"

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include <lib/subghz/subghz_setting.h>
#include <lib/subghz/subghz_worker.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/flipper_application/plugins/plugin_manager.h>
#include <lib/flipper_application/plugins/composite_resolver.h>
#include <dialogs/dialogs.h>
#include "defines.h"
#include "protocols/protocols_common.h"
#include "protocols/protocol_items.h"
#include "protocols/rolljam_protocol_plugins.h"
#ifdef ENABLE_EMULATE_FEATURE
#include "scenes/plugins/rolljam_emulate_plugin.h"
#endif
#include "scenes/plugins/rolljam_psa_bf_plugin.h"

#define ROLLJAM_KEYSTORE_DIR_NAME APP_ASSETS_PATH("encrypted")

typedef struct RollJamApp RollJamApp;

typedef struct {
    SubGhzWorker* worker;
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzRadioPreset* preset;
    const SubGhzProtocolRegistry* protocol_registry;
    CompositeApiResolver* plugin_resolver;
    PluginManager* protocol_plugin_manager;
    const RollJamProtocolPlugin* protocol_plugin;
    RollJamProtocolRegistryFilter protocol_registry_filter;
    RollJamHistory* history;
    const SubGhzDevice* radio_device;
    RollJamTxRxState txrx_state;
    RollJamHopperState hopper_state;
    RollJamRxKeyState rx_key_state;
    uint8_t hopper_idx_frequency;
    uint8_t hopper_timeout;
    uint16_t idx_menu_chosen;
} RollJamTxRx;

struct RollJamApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;
    DialogsApp* dialogs;
    VariableItemList* variable_item_list;
    Submenu* submenu;
    Widget* widget;
    TextInput* text_input;
    View* view_about;
    FuriString* file_path;
    RollJamReceiver* rolljam_receiver;
    RollJamTxRx* txrx;
    SubGhzSetting* setting;
    RollJamLock lock;
    FuriString* loaded_file_path;
    bool auto_save;
    bool radio_initialized;
    RollJamSettings settings;
    uint32_t start_tx_time;
    uint8_t tx_power;
    char save_filename[64];
    FuriString* save_protocol;
    uint16_t save_history_idx;
    bool save_from_saved_info;
    bool emulate_disabled_for_loaded;
    bool emulate_feature_enabled;
    RollJamSelectedCapture selected_capture;
    RollJamCaptureOwner unsaved_history_owner;
    bool shield_auto_save_failed;
#ifdef ENABLE_EMULATE_FEATURE
#define EMULATE_NAV_NONE     0U
#define EMULATE_NAV_POP      1U
#define EMULATE_NAV_STOP_APP 2U
    CompositeApiResolver* emulate_plugin_resolver;
    PluginManager* emulate_plugin_manager;
    const RollJamEmulatePlugin* emulate_plugin;
    uint8_t emulate_nav_pending;
#endif
#ifdef ENABLE_PSA_BF_PLUGIN
    CompositeApiResolver* psa_bf_plugin_resolver;
    PluginManager* psa_bf_plugin_manager;
    const RollJamPsaBfPlugin* psa_bf_plugin;
#endif
#ifdef ENABLE_DUAL_RX_SCENE
    RollJamDualReceiver* dual_receiver;
    RollJamRxChain* dual_chain_a;
    RollJamRxChain* dual_chain_b;
    RollJamHistory* dual_history;
    FuriMutex* dual_history_mutex;
    uint32_t dual_freq_a;
    uint32_t dual_freq_b;
    uint8_t dual_preset_a;
    uint8_t dual_preset_b;
#endif
#ifdef ENABLE_SHIELD_RX_SCENE
    RollJamRxChain* shield_rx_chain;
    RollJamTxChain* shield_tx_chain;
    RollJamHistory* shield_history;
    FuriMutex* shield_history_mutex;
    uint32_t shield_freq;
    uint8_t shield_preset_index;
    uint8_t shield_tx_offset_index;
    uint8_t shield_tx_power;
    bool shield_radio_initialized;
#endif
};

#ifdef ENABLE_EMULATE_FEATURE
void rolljam_emulate_context_release(RollJamApp* app);
#endif

typedef enum {
    RollJamSetTypeFord_v0,
    RollJamSetTypeMAX,
} RollJamSetType;

void rolljam_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size);

void rolljam_get_frequency_modulation(
    RollJamApp* app,
    FuriString* frequency,
    FuriString* modulation);
void rolljam_get_frequency_modulation_str(
    RollJamApp* app,
    char* frequency,
    size_t frequency_size,
    char* modulation,
    size_t modulation_size);

void rolljam_begin(RollJamApp* app, uint8_t* preset_data);
uint32_t rolljam_rx(RollJamApp* app, uint32_t frequency);
void rolljam_idle(RollJamApp* app);
void rolljam_rx_end(RollJamApp* app);
void rolljam_sleep(RollJamApp* app);
void rolljam_hopper_update(RollJamApp* app);
void rolljam_tx(RollJamApp* app, uint32_t frequency);
void rolljam_tx_stop(RollJamApp* app);
bool rolljam_radio_init(RollJamApp* app);
void rolljam_radio_deinit(RollJamApp* app);
bool rolljam_refresh_protocol_registry(RollJamApp* app, bool ensure_receiver_ready);
bool rolljam_apply_protocol_registry_for_preset_data(
    RollJamApp* app,
    const uint8_t* preset_data,
    size_t preset_data_size);
bool rolljam_ensure_variable_item_list(RollJamApp* app);
bool rolljam_ensure_widget(RollJamApp* app);
bool rolljam_ensure_text_input(RollJamApp* app);
bool rolljam_ensure_view_about(RollJamApp* app);
bool rolljam_ensure_receiver_view(RollJamApp* app);
void rolljam_release_shared_radio_state(RollJamApp* app);

void rolljam_rx_stack_suspend_for_tx(RollJamApp* app);

void rolljam_rx_stack_resume_after_tx(RollJamApp* app);

void rolljam_app_free(RollJamApp* app);

void rolljam_unload_protocol_plugin(RollJamTxRx* txrx);

static const NotificationSequence sequence_tx = {
    &message_note_c5,
    &message_vibro_on,
    &message_red_255,
    &message_blue_255,
    &message_blink_start_10,
    &message_delay_25,
    &message_vibro_off,
    &message_delay_25,
    &message_sound_off,
    NULL,
};
