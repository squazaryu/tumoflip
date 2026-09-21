#ifdef HID_TRANSPORT_BLE
#include "../hid.h"
#include "../views.h"
#include <stdio.h>
#include <string.h>

enum {
    PeerPair = 1,
    PeerRename,
    PeerForget,
    PeerRefresh,
    PeerFirst = 100
};

bool hid_peer_name_valid(const char* name) {
    bool visible = false;
    for(size_t i = 0; i < HID_PEER_NAME_SIZE; i++) {
        const unsigned char ch = name[i];
        if(ch == 0) return visible;
        if(ch < 32 || ch > 126) return false;
        if(ch != ' ') visible = true;
    }
    return false;
}

static const char* hid_peer_saved_name(Hid* app, const GapBondedDevice* peer) {
    const HidPeerPreferences* prefs = &app->peer_store.preferences;
    for(unsigned i = 0; i < prefs->count; i++) {
        if(!memcmp(peer, &prefs->labels[i].peer, sizeof(*peer)) &&
           hid_peer_name_valid(prefs->labels[i].name)) {
            return prefs->labels[i].name;
        }
    }
    return NULL;
}

bool hid_peer_has_name(Hid* app, const GapBondedDevice* peer) {
    return hid_peer_saved_name(app, peer) != NULL;
}

static bool hid_peer_name_in_use(Hid* app, const char* name) {
    const HidPeerPreferences* prefs = &app->peer_store.preferences;
    for(unsigned i = 0; i < prefs->count; i++) {
        if(strcmp(name, prefs->labels[i].name) == 0) return true;
    }
    return false;
}

void hid_peer_label(Hid* app, const GapBondedDevice* peer, char* out, size_t size) {
    const char* saved = hid_peer_saved_name(app, peer);
    if(saved) {
        snprintf(out, size, "%s", saved);
        return;
    }
    // The bond list contains identities, not host names. Temporary labels do not
    // write to SD; the first explicit selection asks the owner to name the identity.
    unsigned number = 1;
    for(unsigned i = 0; i < app->peer_list.count; i++) {
        const GapBondedDevice* candidate = &app->peer_list.devices[i];
        if(hid_peer_has_name(app, candidate)) continue;
        char label[HID_PEER_NAME_SIZE];
        do {
            snprintf(label, sizeof(label), "Device %u", number++);
        } while(hid_peer_name_in_use(app, label));
        if(!memcmp(candidate, peer, sizeof(*peer))) {
            snprintf(out, size, "%s", label);
            return;
        }
    }
    snprintf(out, size, "Saved device");
}

static void hid_peer_item(void* context, uint32_t event) {
    Hid* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void hid_peer_dialog_result(DialogExResult result, void* context) {
    Hid* app = context;
    hid_peer_item(app, result == DialogExResultRight ? HidPeerDialogConfirm : HidPeerDialogBack);
}

void hid_peer_show_error(Hid* app, const char* text) {
    app->peer_dialog = true;
    app->peer_confirm_forget = false;
    app->peer_pairing_dialog = false;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, hid_peer_dialog_result);
    dialog_ex_set_header(app->dialog, "Bluetooth Remote", 64, 3, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, text, 64, 23, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->dialog, "Back");
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewDialog);
}

bool hid_peer_restart(Hid* app) {
    const bool reconnect = app->peer_active;
    app->peer_active = false;
    bt_disconnect(app->bt);
    app->peer_connected = false;
    app->ble_hid_profile = bt_profile_start_idle(app->bt, ble_profile_hid_ext, &app->ble_hid_cfg);
    if(!app->ble_hid_profile) return false;
    const HidPeerPreferences* prefs = &app->peer_store.preferences;
    if(reconnect && prefs->selected) {
        app->peer_active = bt_set_connection_peer(app->bt, &prefs->peer);
        return app->peer_active;
    }
    return true;
}

void hid_peer_devices_refresh(Hid* app) {
    submenu_reset(app->submenu);
    app->peer_dialog = false;
    app->peer_confirm_forget = false;
    app->peer_pairing_dialog = false;
    if(!app->ble_hid_profile || !bt_get_bonded_devices(app->bt, &app->peer_list)) {
        hid_peer_show_error(app, "Cannot read devices\nBluetooth unavailable");
        return;
    }
    const HidPeerPreferences* prefs = &app->peer_store.preferences;
    submenu_set_header(
        app->submenu,
        app->peer_connected ? "Connected" : (app->peer_active ? "Waiting" : "Choose device"));
    uint32_t selected = PeerPair;
    for(unsigned i = 0; i < app->peer_list.count; i++) {
        const GapBondedDevice* peer = &app->peer_list.devices[i];
        char name[HID_PEER_NAME_SIZE], row[24];
        hid_peer_label(app, peer, name, sizeof(name));
        const bool active = prefs->selected && !memcmp(peer, &prefs->peer, sizeof(*peer));
        snprintf(row, sizeof(row), "%s%s", active ? "* " : "", name);
        submenu_add_item(app->submenu, row, PeerFirst + i, hid_peer_item, app);
        if(active) selected = PeerFirst + i;
    }
    submenu_add_item(app->submenu, "Add device", PeerPair, hid_peer_item, app);
    if(prefs->selected) {
        submenu_add_item(app->submenu, "Rename selected", PeerRename, hid_peer_item, app);
        submenu_add_item(app->submenu, "Forget selected", PeerForget, hid_peer_item, app);
    }
    submenu_add_item(app->submenu, "Refresh devices", PeerRefresh, hid_peer_item, app);
    submenu_set_selected_item(app->submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewSubmenu);
}

void hid_scene_devices_on_enter(void* context) {
    hid_peer_devices_refresh(context);
}

static bool hid_peer_save(Hid* app, const HidPeerPreferences* prefs) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool saved = hid_peer_store_save(storage, &app->peer_store, prefs);
    furi_record_close(RECORD_STORAGE);
    return saved;
}

bool hid_scene_devices_on_event(void* context, SceneManagerEvent event) {
    Hid* app = context;
    if(event.type == SceneManagerEventTypeBack && app->peer_dialog) {
        if(app->peer_pairing_dialog) bt_disconnect(app->bt);
        if(app->peer_pairing_dialog || app->peer_confirm_forget)
            hid_peer_devices_refresh(app);
        else
            scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    const uint32_t id = event.event;
    if(id == HidPeerConnected || id == HidPeerDisconnected) {
        if(!app->peer_dialog) hid_peer_devices_refresh(app);
        return true;
    }
    if(id == HidPeerDialogBack || id == HidPeerDialogConfirm) {
        if(id == HidPeerDialogConfirm && app->peer_confirm_forget) {
            if(!bt_forget_bonded_device(app->bt, &app->peer_store.preferences.peer)) {
                hid_peer_show_error(app, "Removal failed\nReopen to verify");
                return true;
            }
            HidPeerPreferences prefs = app->peer_store.preferences;
            for(unsigned i = 0; i < prefs.count; i++) {
                if(!memcmp(&prefs.labels[i].peer, &prefs.peer, sizeof(prefs.peer))) {
                    memmove(
                        &prefs.labels[i],
                        &prefs.labels[i + 1],
                        (prefs.count - i - 1) * sizeof(prefs.labels[0]));
                    memset(&prefs.labels[--prefs.count], 0, sizeof(prefs.labels[0]));
                    break;
                }
            }
            prefs.selected = 0;
            app->peer_active = false;
            if(!hid_peer_save(app, &prefs)) {
                hid_peer_show_error(app, "Device removed\nSettings not saved");
                return true;
            }
        }
        if(app->peer_pairing_dialog) bt_disconnect(app->bt);
        if(app->peer_pairing_dialog || app->peer_confirm_forget)
            hid_peer_devices_refresh(app);
        else
            scene_manager_previous_scene(app->scene_manager);
    } else if(id >= PeerFirst && id - PeerFirst < app->peer_list.count) {
        bt_disconnect(app->bt);
        app->peer_active = false;
        app->peer_connected = false;
        HidPeerPreferences prefs = app->peer_store.preferences;
        prefs.peer = app->peer_list.devices[id - PeerFirst];
        prefs.selected = 1;
        if(!hid_peer_has_name(app, &prefs.peer)) {
            app->peer_name_target = prefs.peer;
            app->peer_connect_after_name = true;
            scene_manager_next_scene(app->scene_manager, HidScenePeerName);
            return true;
        }
        if(!hid_peer_save(app, &prefs)) {
            hid_peer_show_error(app, "Settings not saved\nCheck SD card");
        } else if(!bt_set_connection_peer(app->bt, &prefs.peer)) {
            hid_peer_show_error(app, "Cannot select peer\nRetry from Devices");
        } else {
            app->peer_active = true;
            scene_manager_previous_scene(app->scene_manager);
        }
    } else if(id == PeerPair) {
        app->peer_active = false;
        app->peer_connected = false;
        if(!bt_set_connection_peer(app->bt, NULL)) {
            hid_peer_show_error(app, "Cannot start pairing\nRetry from Devices");
        } else {
            hid_peer_show_error(app, "Pair in host settings\nDisconnect old hosts");
            app->peer_pairing_dialog = true;
            dialog_ex_set_header(app->dialog, "Add device", 64, 3, AlignCenter, AlignTop);
            dialog_ex_set_left_button_text(app->dialog, "Done");
        }
    } else if(id == PeerRename) {
        app->peer_name_target = app->peer_store.preferences.peer;
        app->peer_connect_after_name = false;
        scene_manager_next_scene(app->scene_manager, HidScenePeerName);
    } else if(id == PeerForget) {
        hid_peer_show_error(app, "Only selected device\nwill be unpaired");
        app->peer_confirm_forget = true;
        dialog_ex_set_right_button_text(app->dialog, "Forget");
    } else if(id == PeerRefresh) {
        hid_peer_devices_refresh(app);
    }
    return true;
}

void hid_scene_devices_on_exit(void* context) {
    Hid* app = context;
    if(app->peer_pairing_dialog) bt_disconnect(app->bt);
    app->peer_pairing_dialog = false;
    app->peer_dialog = false;
    dialog_ex_reset(app->dialog);
    submenu_reset(app->submenu);
}
#endif
