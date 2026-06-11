// Quac App Bridge action.
//
// A ".qab" file holds a single line:  app_id|command|payload   (payload optional)
// e.g.  sber_relay|toggle|
//
// On activation it emits a BLE App Bridge frame via the firmware's
// bt_app_bridge_send_text(). It does NOT run its own bridge — the host-side
// app_bridge_daemon receives the frame and routes it (e.g. sber_relay/toggle ->
// Home Assistant), exactly like the Flipper Relay app does.
//
// Requires the Unleashed firmware App Bridge API and the default serial BLE
// profile to be active (Quac does not switch profiles, so it is).

#include "quac.h"
#include "item.h"
#include "action_i.h"
#include <bt/bt_service/bt.h>

void action_appbridge_tx(void* context, const FuriString* action_path, FuriString* error) {
    App* app = (App*)context;

    FuriString* content = furi_string_alloc();
    File* file = storage_file_alloc(app->storage);
    bool read_ok = false;
    do {
        if(!storage_file_open(
               file, furi_string_get_cstr(action_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
            ACTION_SET_ERROR("AppBridge: can't open %s", furi_string_get_cstr(action_path));
            break;
        }
        char buf[129];
        size_t n;
        while((n = storage_file_read(file, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            furi_string_cat_str(content, buf);
            if(n < sizeof(buf) - 1) break;
        }
        read_ok = true;
    } while(false);
    storage_file_close(file);
    storage_file_free(file);
    if(!read_ok) {
        furi_string_free(content);
        return;
    }

    furi_string_trim(content);

    // Split "app_id|command|payload"
    FuriString* app_id = furi_string_alloc();
    FuriString* command = furi_string_alloc();
    FuriString* payload = furi_string_alloc();

    size_t p1 = furi_string_search_char(content, '|');
    if(p1 == FURI_STRING_FAILURE) {
        ACTION_SET_ERROR("AppBridge: need app_id|command");
    } else {
        furi_string_set_n(app_id, content, 0, p1);
        FuriString* rest = furi_string_alloc();
        furi_string_set_n(rest, content, p1 + 1, furi_string_size(content) - p1 - 1);
        size_t p2 = furi_string_search_char(rest, '|');
        if(p2 == FURI_STRING_FAILURE) {
            furi_string_set(command, rest);
        } else {
            furi_string_set_n(command, rest, 0, p2);
            furi_string_set_n(payload, rest, p2 + 1, furi_string_size(rest) - p2 - 1);
        }
        furi_string_free(rest);
        furi_string_trim(app_id);
        furi_string_trim(command);
        furi_string_trim(payload);

        if(furi_string_empty(app_id) || furi_string_empty(command)) {
            ACTION_SET_ERROR("AppBridge: empty app_id/command");
        } else {
            Bt* bt = furi_record_open(RECORD_BT);
            bool sent = bt_app_bridge_send_text(
                bt,
                furi_string_get_cstr(app_id),
                furi_string_get_cstr(command),
                furi_string_get_cstr(payload));
            furi_record_close(RECORD_BT);
            if(!sent) {
                ACTION_SET_ERROR("AppBridge: send failed (BLE connected?)");
            } else {
                FURI_LOG_I(
                    TAG,
                    "AppBridge sent: %s/%s",
                    furi_string_get_cstr(app_id),
                    furi_string_get_cstr(command));
            }
        }
    }

    furi_string_free(app_id);
    furi_string_free(command);
    furi_string_free(payload);
    furi_string_free(content);
}
