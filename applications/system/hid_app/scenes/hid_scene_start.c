#include "../hid.h"
#include "../views.h"

enum HidSubmenuIndex {
    HidSubmenuIndexKeynote,
    HidSubmenuIndexKeynoteVertical,
    HidSubmenuIndexKeyboard,
    HidSubmenuIndexNumpad,
    HidSubmenuIndexMedia,
    HidSubmenuIndexMusicMacOs,
    HidSubmenuIndexMovie,
    HidSubmenuIndexTikTok,
    HidSubmenuIndexMouse,
    HidSubmenuIndexMouseClicker,
    HidSubmenuIndexMouseJiggler,
    HidSubmenuIndexMouseJigglerStealth,
    HidSubmenuIndexPushToTalk,
    HidSubmenuIndexRename,
    HidSubmenuIndexRemovePairing,
    HidSubmenuIndexDevices,
};

static void hid_scene_start_submenu_callback(void* context, uint32_t index) {
    furi_assert(context);
    Hid* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void hid_scene_start_on_enter(void* context) {
    Hid* app = context;
#ifdef HID_TRANSPORT_BLE
    submenu_set_header(
        app->submenu,
        app->peer_connected ? "Connected" : (app->peer_active ? "Waiting" : "Bluetooth Remote"));
    if(app->peer_store.preferences.selected) {
        char name[HID_PEER_NAME_SIZE];
        hid_peer_label(app, &app->peer_store.preferences.peer, name, sizeof(name));
        snprintf(app->peer_header, sizeof(app->peer_header), "Device: %s", name);
    } else {
        snprintf(app->peer_header, sizeof(app->peer_header), "Device");
    }
    submenu_add_item(
        app->submenu,
        app->peer_header,
        HidSubmenuIndexDevices,
        hid_scene_start_submenu_callback,
        app);
#endif
    submenu_add_item(
        app->submenu, "Keynote", HidSubmenuIndexKeynote, hid_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu,
        "Keynote Vertical",
        HidSubmenuIndexKeynoteVertical,
        hid_scene_start_submenu_callback,
        app);
    submenu_add_item(
        app->submenu, "Keyboard", HidSubmenuIndexKeyboard, hid_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Numpad", HidSubmenuIndexNumpad, hid_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Media", HidSubmenuIndexMedia, hid_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu,
        "Apple Music macOS",
        HidSubmenuIndexMusicMacOs,
        hid_scene_start_submenu_callback,
        app);
    submenu_add_item(
        app->submenu, "Movie", HidSubmenuIndexMovie, hid_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Mouse", HidSubmenuIndexMouse, hid_scene_start_submenu_callback, app);
    submenu_add_item(
        app->submenu,
        "TikTok / YT Shorts",
        HidSubmenuIndexTikTok,
        hid_scene_start_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Mouse Clicker",
        HidSubmenuIndexMouseClicker,
        hid_scene_start_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Mouse Jiggler",
        HidSubmenuIndexMouseJiggler,
        hid_scene_start_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Mouse Jiggler Stealth",
        HidSubmenuIndexMouseJigglerStealth,
        hid_scene_start_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "PushToTalk",
        HidSubmenuIndexPushToTalk,
        hid_scene_start_submenu_callback,
        app);
#ifdef HID_TRANSPORT_BLE
    submenu_add_item(
        app->submenu,
        "Bluetooth Remote Name",
        HidSubmenuIndexRename,
        hid_scene_start_submenu_callback,
        app);
    submenu_add_item(
        app->submenu,
        "Forget all devices",
        HidSubmenuIndexRemovePairing,
        hid_scene_start_submenu_callback,
        app);
#endif

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, HidSceneStart));
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewSubmenu);
}

bool hid_scene_start_on_event(void* context, SceneManagerEvent event) {
    Hid* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
#ifdef HID_TRANSPORT_BLE
        if(event.event == HidPeerConnected || event.event == HidPeerDisconnected) {
            submenu_set_header(
                app->submenu,
                app->peer_connected ? "Connected" :
                                      (app->peer_active ? "Waiting" : "Bluetooth Remote"));
            return true;
        }
        if(event.event == HidSubmenuIndexDevices) {
            scene_manager_next_scene(app->scene_manager, HidSceneDevices);
            return true;
        }
#endif
        if(event.event == HidSubmenuIndexRemovePairing) {
            scene_manager_next_scene(app->scene_manager, HidSceneUnpair);
        } else if(event.event == HidSubmenuIndexRename) {
            scene_manager_next_scene(app->scene_manager, HidSceneRename);
        } else {
            HidView view_id;

            switch(event.event) {
            case HidSubmenuIndexKeynote:
                view_id = HidViewKeynote;
                hid_keynote_set_orientation(app->hid_keynote, false);
                break;
            case HidSubmenuIndexKeynoteVertical:
                view_id = HidViewKeynote;
                hid_keynote_set_orientation(app->hid_keynote, true);
                break;
            case HidSubmenuIndexKeyboard:
                view_id = HidViewKeyboard;
                break;
            case HidSubmenuIndexNumpad:
                view_id = HidViewNumpad;
                break;
            case HidSubmenuIndexMedia:
                view_id = HidViewMedia;
                break;
            case HidSubmenuIndexMusicMacOs:
                view_id = HidViewMusicMacOs;
                break;
            case HidSubmenuIndexMovie:
                view_id = HidViewMovie;
                break;
            case HidSubmenuIndexTikTok:
                view_id = BtHidViewTikTok;
                break;
            case HidSubmenuIndexMouse:
                view_id = HidViewMouse;
                break;
            case HidSubmenuIndexMouseClicker:
                view_id = HidViewMouseClicker;
                break;
            case HidSubmenuIndexMouseJiggler:
                view_id = HidViewMouseJiggler;
                break;
            case HidSubmenuIndexMouseJigglerStealth:
                view_id = HidViewMouseJigglerStealth;
                break;
            case HidSubmenuIndexPushToTalk:
                view_id = HidViewPushToTalkMenu;
                break;
            default:
                furi_crash();
            }

            scene_manager_set_scene_state(app->scene_manager, HidSceneMain, view_id);
            scene_manager_next_scene(app->scene_manager, HidSceneMain);
        }

        scene_manager_set_scene_state(app->scene_manager, HidSceneStart, event.event);
        consumed = true;
    }

    return consumed;
}

void hid_scene_start_on_exit(void* context) {
    Hid* app = context;
    submenu_reset(app->submenu);
}
