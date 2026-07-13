#include "../../applications_user/tumovm_peripherals/tumovm_peripheral_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static TumoVmPeripheralManifestInput usb_input(void) {
    const TumoVmPeripheralManifestInput input = {
        .version = 1,
        .id = "usb_media",
        .name = "USB Media Key",
        .adapter = "usb.hid.consumer",
        .action = "play_pause",
        .minimum_api = 88,
        .maximum_api = 88,
        .require_confirmation = true,
    };
    return input;
}

int main(void) {
    TumoVmPeripheralManifest manifest;
    TumoVmPeripheralManifestInput input = usb_input();
    assert(tumovm_peripheral_manifest_build(&input, 88, &manifest) == TumoVmPeripheralManifestOk);
    assert(manifest.adapter == TumoVmPeripheralAdapterUsbHidConsumer);
    assert(manifest.action == TumoVmPeripheralActionPlayPause);
    assert(manifest.resources == TumoVmPeripheralResourceUsb);

    input.require_confirmation = false;
    assert(
        tumovm_peripheral_manifest_build(&input, 88, &manifest) ==
        TumoVmPeripheralManifestConfirmationRequired);
    input = usb_input();
    input.action = "keyboard_script";
    assert(
        tumovm_peripheral_manifest_build(&input, 88, &manifest) ==
        TumoVmPeripheralManifestUntrustedAction);
    input = usb_input();
    assert(
        tumovm_peripheral_manifest_build(&input, 87, &manifest) ==
        TumoVmPeripheralManifestApiMismatch);

    const uint8_t aid[] = {0xF0, 0x54, 0x56, 0x4D, 0x01};
    const uint8_t state[] = {0x54, 0x56, 0x4D, 0x01};
    const TumoVmPeripheralManifestInput nfc = {
        .version = 1,
        .id = "nfc_state",
        .name = "NFC State Token",
        .adapter = "nfc.type4",
        .action = "state-v1",
        .minimum_api = 88,
        .maximum_api = 88,
        .aid = aid,
        .aid_size = sizeof(aid),
        .initial_state = state,
        .state_size = sizeof(state),
        .require_confirmation = false,
    };
    assert(tumovm_peripheral_manifest_build(&nfc, 88, &manifest) == TumoVmPeripheralManifestOk);
    assert(manifest.adapter == TumoVmPeripheralAdapterNfcType4);
    assert(manifest.resources == TumoVmPeripheralResourceNfc);
    assert(memcmp(manifest.aid, aid, sizeof(aid)) == 0);

    TumoVmPeripheralManifestInput invalid_nfc = nfc;
    invalid_nfc.state_size = TumoVmPeripheralStateMaximum + 1U;
    assert(
        tumovm_peripheral_manifest_build(&invalid_nfc, 88, &manifest) ==
        TumoVmPeripheralManifestUnsafeConfiguration);

    TumoVmPeripheralResourceRegistry registry;
    tumovm_peripheral_resources_init(&registry);
    assert(tumovm_peripheral_resource_claim(&registry, TumoVmPeripheralResourceUsb));
    assert(!tumovm_peripheral_resource_claim(&registry, TumoVmPeripheralResourceUsb));
    assert(tumovm_peripheral_resource_claim(&registry, TumoVmPeripheralResourceNfc));
    tumovm_peripheral_resource_release(&registry, TumoVmPeripheralResourceUsb);
    assert(tumovm_peripheral_resource_claim(&registry, TumoVmPeripheralResourceUsb));

    puts("tumovm_peripheral_core_host_test: PASS");
    return 0;
}
