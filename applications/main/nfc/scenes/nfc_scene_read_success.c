#include "../helpers/protocol_support/nfc_protocol_support.h"
#include "../nfc_app_i.h"

void nfc_scene_read_success_on_enter(void* context) {
    NfcApp* instance = context;
    if(instance->tumotag_verify_capture) {
        storage_common_mkdir(instance->storage, EXT_PATH("apps_data"));
        storage_common_mkdir(instance->storage, NFC_TUMOTAG_VERIFY_DIR);
        storage_common_remove(instance->storage, NFC_TUMOTAG_VERIFY_OUTPUT);
        nfc_device_save(instance->nfc_device, NFC_TUMOTAG_VERIFY_OUTPUT);
        view_dispatcher_stop(instance->view_dispatcher);
        return;
    }
    nfc_protocol_support_on_enter(NfcProtocolSupportSceneReadSuccess, context);
}

bool nfc_scene_read_success_on_event(void* context, SceneManagerEvent event) {
    return nfc_protocol_support_on_event(NfcProtocolSupportSceneReadSuccess, context, event);
}

void nfc_scene_read_success_on_exit(void* context) {
    nfc_protocol_support_on_exit(NfcProtocolSupportSceneReadSuccess, context);
}
