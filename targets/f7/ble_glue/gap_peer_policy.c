#include "gap_peer_policy.h"
#include <ble/ble.h>
#include <furi.h>
#include <string.h>

#define TAG "BlePeer"

static bool gap_peer_command_ok(const char* operation, tBleStatus status) {
    if(status != BLE_STATUS_SUCCESS) {
        // Keep the failing stage/status available without logging host identities or keys.
        FURI_LOG_E(TAG, "%s failed: 0x%02X", operation, status);
    }
    return status == BLE_STATUS_SUCCESS;
}

bool gap_peer_read(GapBondedDevices* devices) {
    if(!devices) return false;
    memset(devices, 0, sizeof(*devices));
    Bonded_Device_Entry_t entries[GAP_BONDED_DEVICES_MAX];
    uint8_t count = 0;
    if(aci_gap_get_bonded_devices(&count, entries) != BLE_STATUS_SUCCESS ||
       count > GAP_BONDED_DEVICES_MAX) {
        return false;
    }
    for(uint8_t i = 0; i < count; i++) {
        if(entries[i].Address_Type > 1) return false;
    }
    for(uint8_t i = 0; i < count; i++) {
        devices->devices[i].address_type = entries[i].Address_Type;
        memcpy(devices->devices[i].address, entries[i].Address, GAP_MAC_ADDR_SIZE);
    }
    devices->count = count;
    return true;
}

bool gap_peer_select(const GapBondedDevice* peer) {
    if(!peer) {
        // Open pairing is explicit; a failed selected-peer operation never calls this.
        return gap_peer_command_ok("Pairing list", aci_gap_configure_filter_accept_list());
    }
    if(peer->address_type > 1 ||
       aci_gap_is_device_bonded(peer->address_type, peer->address) != BLE_STATUS_SUCCESS) {
        return false;
    }
    List_Entry_t entry = {.Address_Type = peer->address_type};
    memcpy(entry.Address, peer->address, sizeof(entry.Address));
    // Mode 5 replaces BOTH lists from the existing bond, including its peer IRK.
    // A phone changing its private address is still the same bonded identity.
    // ACI owns these lists on the host+controller stack. The raw HCI address-
    // resolution command (0x202D) is exposed only by ST's LL-only stack variants;
    // calling it here rejects every selection before ACI can configure the peer.
    return gap_peer_command_ok("Selected list", aci_gap_add_devices_to_list(1, &entry, 5));
}

bool gap_peer_forget(const GapBondedDevice* peer) {
    return peer && peer->address_type <= 1 &&
           aci_gap_remove_bonded_device(peer->address_type, peer->address) == BLE_STATUS_SUCCESS;
}
