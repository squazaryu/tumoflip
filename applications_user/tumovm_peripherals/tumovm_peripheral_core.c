#include "tumovm_peripheral_core.h"

#include <string.h>

static bool tumovm_peripheral_valid_id(const char* value) {
    if(value == NULL || value[0] == '\0') return false;
    const size_t length = strlen(value);
    if(length >= TumoVmPeripheralIdSize) return false;
    for(size_t index = 0; index < length; index++) {
        const char character = value[index];
        if(!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
             character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

static bool tumovm_peripheral_valid_name(const char* value) {
    if(value == NULL || value[0] == '\0') return false;
    const size_t length = strlen(value);
    if(length >= TumoVmPeripheralNameSize) return false;
    for(size_t index = 0; index < length; index++) {
        const unsigned char character = (unsigned char)value[index];
        if(character < 0x20U || character > 0x7EU) return false;
    }
    return true;
}

static TumoVmPeripheralAdapter tumovm_peripheral_parse_adapter(const char* value) {
    if(value == NULL) return TumoVmPeripheralAdapterNone;
    if(strcmp(value, "usb.hid.consumer") == 0) return TumoVmPeripheralAdapterUsbHidConsumer;
    if(strcmp(value, "nfc.type4") == 0) return TumoVmPeripheralAdapterNfcType4;
    return TumoVmPeripheralAdapterNone;
}

static TumoVmPeripheralAction tumovm_peripheral_parse_action(const char* value) {
    if(value == NULL) return TumoVmPeripheralActionNone;
    if(strcmp(value, "play_pause") == 0) return TumoVmPeripheralActionPlayPause;
    if(strcmp(value, "volume_up") == 0) return TumoVmPeripheralActionVolumeUp;
    if(strcmp(value, "volume_down") == 0) return TumoVmPeripheralActionVolumeDown;
    if(strcmp(value, "state-v1") == 0) return TumoVmPeripheralActionStateV1;
    return TumoVmPeripheralActionNone;
}

TumoVmPeripheralManifestStatus tumovm_peripheral_manifest_build(
    const TumoVmPeripheralManifestInput* input,
    uint32_t current_api,
    TumoVmPeripheralManifest* manifest) {
    if(input == NULL || manifest == NULL) return TumoVmPeripheralManifestInvalidArgument;
    memset(manifest, 0, sizeof(*manifest));

    if(input->version != TumoVmPeripheralManifestVersion) {
        return TumoVmPeripheralManifestUnsupportedVersion;
    }
    if(!tumovm_peripheral_valid_id(input->id) || !tumovm_peripheral_valid_name(input->name)) {
        return TumoVmPeripheralManifestInvalidIdentity;
    }
    if(input->minimum_api > current_api || input->maximum_api < current_api ||
       input->minimum_api > input->maximum_api) {
        return TumoVmPeripheralManifestApiMismatch;
    }

    const TumoVmPeripheralAdapter adapter = tumovm_peripheral_parse_adapter(input->adapter);
    if(adapter == TumoVmPeripheralAdapterNone) {
        return TumoVmPeripheralManifestUntrustedAdapter;
    }
    const TumoVmPeripheralAction action = tumovm_peripheral_parse_action(input->action);
    if(action == TumoVmPeripheralActionNone) {
        return TumoVmPeripheralManifestUntrustedAction;
    }

    uint32_t resources = TumoVmPeripheralResourceNone;
    if(adapter == TumoVmPeripheralAdapterUsbHidConsumer) {
        if(action != TumoVmPeripheralActionPlayPause && action != TumoVmPeripheralActionVolumeUp &&
           action != TumoVmPeripheralActionVolumeDown) {
            return TumoVmPeripheralManifestUntrustedAction;
        }
        if(!input->require_confirmation) {
            return TumoVmPeripheralManifestConfirmationRequired;
        }
        if(input->aid_size != 0U || input->state_size != 0U) {
            return TumoVmPeripheralManifestUnsafeConfiguration;
        }
        resources = TumoVmPeripheralResourceUsb;
    } else if(adapter == TumoVmPeripheralAdapterNfcType4) {
        if(action != TumoVmPeripheralActionStateV1) {
            return TumoVmPeripheralManifestUntrustedAction;
        }
        if(input->require_confirmation || input->aid == NULL || input->aid_size < 5U ||
           input->aid_size > TumoVmPeripheralAidMaximum || input->initial_state == NULL ||
           input->state_size == 0U || input->state_size > TumoVmPeripheralStateMaximum) {
            return TumoVmPeripheralManifestUnsafeConfiguration;
        }
        resources = TumoVmPeripheralResourceNfc;
    }

    manifest->version = input->version;
    memcpy(manifest->id, input->id, strlen(input->id) + 1U);
    memcpy(manifest->name, input->name, strlen(input->name) + 1U);
    manifest->adapter = adapter;
    manifest->action = action;
    manifest->resources = resources;
    manifest->require_confirmation = input->require_confirmation;
    if(input->aid_size > 0U) {
        memcpy(manifest->aid, input->aid, input->aid_size);
        manifest->aid_size = (uint8_t)input->aid_size;
    }
    if(input->state_size > 0U) {
        memcpy(manifest->initial_state, input->initial_state, input->state_size);
        manifest->state_size = (uint8_t)input->state_size;
    }
    return TumoVmPeripheralManifestOk;
}

void tumovm_peripheral_resources_init(TumoVmPeripheralResourceRegistry* registry) {
    if(registry != NULL) registry->claimed = 0U;
}

bool tumovm_peripheral_resource_claim(
    TumoVmPeripheralResourceRegistry* registry,
    uint32_t resources) {
    if(registry == NULL || resources == 0U || (registry->claimed & resources) != 0U) return false;
    registry->claimed |= resources;
    return true;
}

void tumovm_peripheral_resource_release(
    TumoVmPeripheralResourceRegistry* registry,
    uint32_t resources) {
    if(registry != NULL) registry->claimed &= ~resources;
}

const char* tumovm_peripheral_manifest_status_name(TumoVmPeripheralManifestStatus status) {
    switch(status) {
    case TumoVmPeripheralManifestOk:
        return "Ready";
    case TumoVmPeripheralManifestUnsupportedVersion:
        return "Wrong version";
    case TumoVmPeripheralManifestInvalidIdentity:
        return "Invalid identity";
    case TumoVmPeripheralManifestApiMismatch:
        return "API mismatch";
    case TumoVmPeripheralManifestUntrustedAdapter:
        return "Blocked adapter";
    case TumoVmPeripheralManifestUntrustedAction:
        return "Blocked action";
    case TumoVmPeripheralManifestUnsafeConfiguration:
        return "Unsafe config";
    case TumoVmPeripheralManifestConfirmationRequired:
        return "Confirm required";
    default:
        return "Invalid package";
    }
}

const char* tumovm_peripheral_adapter_name(TumoVmPeripheralAdapter adapter) {
    switch(adapter) {
    case TumoVmPeripheralAdapterUsbHidConsumer:
        return "USB HID";
    case TumoVmPeripheralAdapterNfcType4:
        return "NFC Type 4";
    default:
        return "Unknown";
    }
}

const char* tumovm_peripheral_action_name(TumoVmPeripheralAction action) {
    switch(action) {
    case TumoVmPeripheralActionPlayPause:
        return "Play/Pause";
    case TumoVmPeripheralActionVolumeUp:
        return "Volume Up";
    case TumoVmPeripheralActionVolumeDown:
        return "Volume Down";
    case TumoVmPeripheralActionStateV1:
        return "State v1";
    default:
        return "None";
    }
}
