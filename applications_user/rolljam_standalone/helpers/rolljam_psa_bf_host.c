#include "../rolljam_app_i.h"
#include "rolljam_psa_bf_host.h"
#include "../rolljam_history.h"
#include "../protocols/protocols_common.h"
// #include "../scenes/plugins/rolljam_psa_bf_plugin.h"

#include <loader/firmware_api/firmware_api.h>
#include <lib/flipper_application/plugins/plugin_manager.h>
#include <lib/flipper_application/plugins/composite_resolver.h>
#include <notification/notification_messages.h>

#define TAG ROLLJAM_PSA_BF_HOST_TAG
// #define PSA_BF_PLUGIN_PATH APP_ASSETS_PATH("plugins/rolljam_psa_bf_plugin.fal")

static bool host_ensure_widget(void* app) {
    return rolljam_ensure_widget((RollJamApp*)app);
}

static Widget* host_get_widget(void* app) {
    RollJamApp* a = (RollJamApp*)app;
    return a ? a->widget : NULL;
}

static FlipperFormat* host_get_history_flipper_format(void* app) {
    RollJamApp* a = (RollJamApp*)app;
    if(!a) return NULL;
    return rolljam_selected_capture_get_raw_data(a);
}

static uint16_t host_get_history_index(void* app) {
    RollJamApp* a = (RollJamApp*)app;
    return a ? rolljam_selected_capture_get_index(a) : 0;
}

static void host_set_history_index(void* app, uint16_t idx) {
    RollJamApp* a = (RollJamApp*)app;
    if(a) {
        a->selected_capture.index = idx;
        if(a->txrx) a->txrx->idx_menu_chosen = idx;
    }
}

static RollJamHistory* host_get_history(void* app) {
    RollJamApp* a = (RollJamApp*)app;
    return a ? rolljam_selected_capture_get_history(a) : NULL;
}

static void host_history_set_item_str(void* app, uint16_t idx, const char* str) {
    RollJamHistory* history = host_get_history(app);
    if(history) rolljam_history_set_item_str(history, idx, str);
}

static void host_patch_flipper_format_on_success(FlipperFormat* ff, const PsaBfState* s) {
    if(!ff || !s) return;
    flipper_format_rewind(ff);
    flipper_format_insert_or_update_uint32(ff, FF_SERIAL, &s->decrypted_serial, 1);
    uint32_t btn = s->decrypted_button;
    flipper_format_insert_or_update_uint32(ff, FF_BTN, &btn, 1);
    flipper_format_insert_or_update_uint32(ff, FF_CNT, &s->decrypted_counter, 1);
    uint32_t type = s->decrypted_type;
    flipper_format_insert_or_update_uint32(ff, FF_TYPE, &type, 1);
    uint32_t crc_val = s->decrypted_crc;
    flipper_format_insert_or_update_uint32(ff, "CRC", &crc_val, 1);
    flipper_format_insert_or_update_uint32(ff, "Seed", &s->decrypted_seed, 1);
}

static void host_send_custom_event(void* app, uint32_t event) {
    RollJamApp* a = (RollJamApp*)app;
    if(a) view_dispatcher_send_custom_event(a->view_dispatcher, event);
}

static void host_notification_error(void* app) {
    RollJamApp* a = (RollJamApp*)app;
    if(a) notification_message(a->notifications, &sequence_error);
}

static void host_notification_success(void* app) {
    RollJamApp* a = (RollJamApp*)app;
    if(a) notification_message(a->notifications, &sequence_success);
}

static void host_receiver_info_rebuild_widget(void* app) {
    rolljam_receiver_info_rebuild_normal_widget(app);
}

#ifdef ENABLE_SUB_DECODE_SCENE
static void host_subdecode_signal_info_refresh(void* app) {
    rolljam_subdecode_psa_bf_complete_refresh(app);
}
#else
static void host_subdecode_signal_info_refresh(void* app) {
    UNUSED(app);
}
#endif

static void host_scene_previous(void* app) {
    RollJamApp* a = (RollJamApp*)app;
    if(a) scene_manager_previous_scene(a->scene_manager);
}

static const RollJamPsaBfHostApi rolljam_psa_bf_host_api = {
    .ensure_widget = host_ensure_widget,
    .get_widget = host_get_widget,
    .get_history_flipper_format = host_get_history_flipper_format,
    .get_history_index = host_get_history_index,
    .set_history_index = host_set_history_index,
    .get_history = host_get_history,
    .history_set_item_str = host_history_set_item_str,
    .patch_flipper_format_on_success = host_patch_flipper_format_on_success,
    .send_custom_event = host_send_custom_event,
    .notification_error = host_notification_error,
    .notification_success = host_notification_success,
    .receiver_info_rebuild_widget = host_receiver_info_rebuild_widget,
    .subdecode_signal_info_refresh = host_subdecode_signal_info_refresh,
    .scene_previous = host_scene_previous,
};

static void psa_bf_plugin_unload(RollJamApp* app) {
    furi_check(app);
    app->psa_bf_plugin = NULL;
}

// bool rolljam_psa_bf_plugin_ensure_loaded(RollJamApp* app) {
//     furi_check(app);
//
//     if(app->psa_bf_plugin) return true;
//
//     app->psa_bf_plugin = &rolljam_psa_bf_plugin;
//     rolljam_psa_bf_plugin.set_host_api(&rolljam_psa_bf_host_api);
//     return true;
// }

// void rolljam_psa_bf_plugin_unload_if_idle(RollJamApp* app) {
//     if(!app) return;
//     if(app->psa_bf_plugin && app->psa_bf_plugin->is_running && app->psa_bf_plugin->is_running(app)) {
//         return;
//     }
//     psa_bf_plugin_unload(app);
// }

void rolljam_psa_bf_context_release(RollJamApp* app) {
    if(!app) return;
    if(app->psa_bf_plugin && app->psa_bf_plugin->context_release) {
        app->psa_bf_plugin->context_release(app);
    }
    psa_bf_plugin_unload(app);
}
