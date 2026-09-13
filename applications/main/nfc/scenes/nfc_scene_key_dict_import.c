#include "../nfc_app_i.h"

static void
    nfc_scene_key_dict_import_show_result(NfcApp* instance, const NfcKeyDictImportStats* stats) {
    Popup* popup = instance->popup;

    if(stats->status == NfcKeyDictImportStatusSuccess) {
        popup_set_header(
            popup,
            stats->added > 0 ? "Keys Saved" : "Nothing to Add",
            64,
            3,
            AlignCenter,
            AlignTop);
        nfc_text_store_set(
            instance,
            "New keys: %u\nAlready known: %u",
            (unsigned)stats->added,
            (unsigned)stats->known);
    } else {
        notification_message(instance->notifications, &sequence_error);
        popup_set_header(popup, "Save Failed", 64, 3, AlignCenter, AlignTop);

        switch(stats->status) {
        case NfcKeyDictImportStatusSystemDictionaryMissing:
            nfc_text_store_set(instance, "System dictionary\nis missing\nNothing changed");
            break;
        case NfcKeyDictImportStatusDictionaryReadFailed:
            nfc_text_store_set(instance, "Cannot read dictionary\nDictionary unchanged");
            break;
        case NfcKeyDictImportStatusBackupFailed:
            nfc_text_store_set(instance, "Cannot make backup\nDictionary unchanged");
            break;
        case NfcKeyDictImportStatusWriteFailed:
            nfc_text_store_set(instance, "Dictionary unchanged\nCheck the SD card");
            break;
        case NfcKeyDictImportStatusRollbackFailed:
            nfc_text_store_set(
                instance,
                stats->backup_preserved ? "Backup preserved\nin NFC assets\nRecovery required" :
                                          "Check user dictionary\nRecovery required");
            break;
        default:
            break;
        }
    }

    popup_set_text(popup, instance->text_store, 4, 26, AlignLeft, AlignTop);
    view_dispatcher_switch_to_view(instance->view_dispatcher, NfcViewPopup);
}

void nfc_scene_key_dict_import_on_enter(void* context) {
    NfcApp* instance = context;
    const NfcKeyDict* dict = nfc_key_dict(instance->key_dict_type);

    nfc_show_loading_label_popup(instance, "Saving keys to\nuser dictionary", true);

    uint8_t* keys = malloc(NFC_KEY_DICT_DEVICE_KEYS_MAX * dict->key_size);
    const size_t key_count = nfc_key_dict_collect_from_device(
        instance->key_dict_type, instance->nfc_device, keys, NFC_KEY_DICT_DEVICE_KEYS_MAX);
    NfcKeyDictImportStats stats = {};
    nfc_key_dict_import(instance->key_dict_type, keys, key_count, &stats);
    free(keys);

    nfc_show_loading_label_popup(instance, NULL, false);
    if(stats.added > 0) dolphin_deed(DolphinDeedNfcKeyAdd);
    nfc_scene_key_dict_import_show_result(instance, &stats);
}

bool nfc_scene_key_dict_import_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void nfc_scene_key_dict_import_on_exit(void* context) {
    NfcApp* instance = context;
    popup_reset(instance->popup);
}
