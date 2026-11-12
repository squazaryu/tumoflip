#ifdef HID_TRANSPORT_BLE
#include "../hid.h"
#include "../views.h"
#include <string.h>

static void hid_peer_name_done(void* context) {
    Hid* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, HidPeerNameDone);
}

void hid_scene_peer_name_on_enter(void* context) {
    Hid* app = context;
    hid_peer_label(app, &app->peer_name_target, app->peer_name, sizeof(app->peer_name));
    text_input_reset(app->text_input);
    text_input_set_header_text(
        app->text_input, app->peer_connect_after_name ? "Name this device" : "Rename device");
    text_input_set_result_callback(
        app->text_input, hid_peer_name_done, app, app->peer_name, sizeof(app->peer_name), true);
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewTextInput);
}

bool hid_scene_peer_name_on_event(void* context, SceneManagerEvent event) {
    Hid* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == HidPeerConnected || event.event == HidPeerDisconnected) return true;
    if(event.event == HidPeerDialogBack) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    if(event.event != HidPeerNameDone) return false;
    if(!hid_peer_name_valid(app->peer_name)) {
        hid_peer_show_error(app, "Enter a device name\nUse 1-12 characters");
        return true;
    }
    HidPeerPreferences prefs = app->peer_store.preferences;
    prefs.peer = app->peer_name_target;
    prefs.selected = 1;
    unsigned index = 0;
    while(index < prefs.count &&
          memcmp(&prefs.labels[index].peer, &prefs.peer, sizeof(prefs.peer)))
        index++;
    if(index == GAP_BONDED_DEVICES_MAX) {
        hid_peer_show_error(app, "Name list is full");
        return true;
    }
    if(index == prefs.count) prefs.count++;
    prefs.labels[index].peer = prefs.peer;
    memcpy(prefs.labels[index].name, app->peer_name, sizeof(app->peer_name));
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool saved = hid_peer_store_save(storage, &app->peer_store, &prefs);
    furi_record_close(RECORD_STORAGE);
    if(!saved) {
        hid_peer_show_error(app, "Name not saved\nCheck name and SD");
    } else if(app->peer_connect_after_name) {
        if(bt_set_connection_peer(app->bt, &prefs.peer)) {
            app->peer_active = true;
            scene_manager_search_and_switch_to_previous_scene(app->scene_manager, HidSceneStart);
        } else {
            hid_peer_show_error(app, "Cannot select peer\nRetry from Devices");
        }
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
    return true;
}

void hid_scene_peer_name_on_exit(void* context) {
    Hid* app = context;
    text_input_reset(app->text_input);
    dialog_ex_reset(app->dialog);
    app->peer_dialog = false;
    app->peer_connect_after_name = false;
}
#endif
