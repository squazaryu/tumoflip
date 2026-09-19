// Methods for NFC transmission

// nfc
#include <furi.h>
#include <furi_hal.h>
#include <nfc/nfc.h>
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>

#include "action_i.h"
#include "quac.h"

void action_nfc_tx(void* context, const FuriString* action_path, FuriString* error) {
    App* app = context;
    if(!quac_duration_valid(app->settings.nfc_duration)) {
        ACTION_SET_ERROR("NFC: Duration must be 100..60000 ms");
        return;
    }

    FURI_LOG_I(TAG, "NFC: Tx %s", furi_string_get_cstr(action_path));
    Nfc* nfc = nfc_alloc();
    NfcDevice* device = nfc_device_alloc();

    if(nfc_device_load(device, furi_string_get_cstr(action_path))) {
        NfcProtocol protocol = nfc_device_get_protocol(device);
        FURI_LOG_I(TAG, "NFC: Protocol %s", nfc_device_get_protocol_name(protocol));
        NfcListener* listener =
            nfc_listener_alloc(nfc, protocol, nfc_device_get_data(device, protocol));
        FURI_LOG_I(TAG, "NFC: Starting...");
        QuacActionWait* wait = quac_action_wait_alloc(app);
        nfc_listener_start(listener, NULL, NULL);

        if(!quac_action_wait_run(wait, app->settings.nfc_duration) && !app->action_cancelled)
            ACTION_SET_ERROR("NFC: Wait failed");

        FURI_LOG_I(TAG, "NFC: Done");
        nfc_listener_stop(listener);
        nfc_listener_free(listener);
        quac_action_wait_free(wait);
    } else {
        FURI_LOG_E(TAG, "NFC: Failed to load %s", furi_string_get_cstr(action_path));
        ACTION_SET_ERROR("Failed to load %s", furi_string_get_cstr(action_path));
    }
    nfc_device_clear(device); // probably not needed?
    nfc_free(nfc);
    nfc_device_free(device);
}
