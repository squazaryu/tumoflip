#pragma once
#include <gap.h>
#include <storage/storage.h>

#define HID_PEER_NAME_SIZE 13
typedef struct {
    GapBondedDevice peer;
    char name[HID_PEER_NAME_SIZE];
} HidPeerLabel;

typedef struct {
    GapBondedDevice peer;
    uint8_t selected;
    uint8_t count;
    HidPeerLabel labels[GAP_BONDED_DEVICES_MAX];
} HidPeerPreferences;

typedef struct {
    HidPeerPreferences preferences;
    uint32_t sequence;
    uint8_t slot;
} HidPeerStore;

bool hid_peer_store_load(Storage* storage, HidPeerStore* store);
bool hid_peer_store_save(Storage* storage, HidPeerStore* store, const HidPeerPreferences* value);
