#include "hid_peer_store.h"
#include <toolbox/crc32_calc.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    HidPeerPreferences value;
    uint8_t reserved[3];
    uint32_t checksum;
} HidPeerRecord;

// Version 1, little-endian F7 preferences. No pairing credentials are stored here.
_Static_assert(sizeof(HidPeerRecord) == 724, "HID preference format layout changed");
#define HID_PEER_RECORD_MAGIC 0x31525048U
static const char* const hid_peer_paths[] = {
    APP_DATA_PATH(".bt_hid.targets.a"),
    APP_DATA_PATH(".bt_hid.targets.b")};

static bool hid_peer_value_valid(const HidPeerPreferences* value) {
    if(value->selected > 1 || value->peer.address_type > 1 ||
       value->count > GAP_BONDED_DEVICES_MAX)
        return false;
    for(unsigned i = 0; i < value->count; i++) {
        const HidPeerLabel* label = &value->labels[i];
        if(label->peer.address_type > 1 || !memchr(label->name, 0, sizeof(label->name)))
            return false;
        for(const char* ch = label->name; *ch; ch++) {
            if(*ch < 32 || *ch > 126) return false;
        }
        for(unsigned j = 0; j < i; j++) {
            if(memcmp(&label->peer, &value->labels[j].peer, sizeof(label->peer)) == 0)
                return false;
        }
    }
    return true;
}

// Missing, valid and invalid are distinct. A valid journal record wins over a
// interrupted write to its inactive slot. Startup still requires explicit selection.
static int hid_peer_read_record(Storage* storage, unsigned slot, HidPeerRecord* record) {
    FileInfo info;
    const FS_Error status = storage_common_stat(storage, hid_peer_paths[slot], &info);
    if(status == FSE_NOT_EXIST) return 0;
    if(status != FSE_OK || info.size != sizeof(*record)) return -1;
    File* file = storage_file_alloc(storage);
    const bool opened =
        storage_file_open(file, hid_peer_paths[slot], FSAM_READ, FSOM_OPEN_EXISTING);
    bool ok = opened && storage_file_read(file, record, sizeof(*record)) == sizeof(*record);
    if(opened && !storage_file_close(file)) ok = false;
    storage_file_free(file);
    if(!ok || record->magic != HID_PEER_RECORD_MAGIC || !hid_peer_value_valid(&record->value) ||
       record->checksum != crc32_calc_buffer(0, record, offsetof(HidPeerRecord, checksum)))
        return -1;
    return 1;
}

bool hid_peer_store_load(Storage* storage, HidPeerStore* store) {
    memset(store, 0, sizeof(*store));
    HidPeerRecord record;
    bool found = false, invalid = false;
    for(unsigned slot = 0; slot < 2; slot++) {
        const int result = hid_peer_read_record(storage, slot, &record);
        if(result < 0) invalid = true;
        if(result == 1 &&
           (!found || (record.sequence != store->sequence &&
                       (uint32_t)(record.sequence - store->sequence) < 0x80000000U))) {
            store->preferences = record.value;
            store->sequence = record.sequence;
            store->slot = slot;
            found = true;
        }
    }
    return found || !invalid;
}

bool hid_peer_store_save(Storage* storage, HidPeerStore* store, const HidPeerPreferences* value) {
    if(!hid_peer_value_valid(value)) return false;
    HidPeerRecord record = {0};
    record.magic = HID_PEER_RECORD_MAGIC;
    record.sequence = store->sequence + 1;
    record.value = *value;
    record.checksum = crc32_calc_buffer(0, &record, offsetof(HidPeerRecord, checksum));
    const unsigned slot = store->slot ^ 1U;
    File* file = storage_file_alloc(storage);
    const bool opened =
        storage_file_open(file, hid_peer_paths[slot], FSAM_WRITE, FSOM_CREATE_ALWAYS);
    bool ok = opened && storage_file_write(file, &record, sizeof(record)) == sizeof(record) &&
              storage_file_sync(file);
    if(opened && !storage_file_close(file)) ok = false;
    storage_file_free(file);
    if(!ok) return false;
    const uint32_t sequence = record.sequence;
    if(hid_peer_read_record(storage, slot, &record) != 1 || record.sequence != sequence ||
       memcmp(&record.value, value, sizeof(*value)))
        return false;
    store->preferences = *value;
    store->sequence = sequence;
    store->slot = slot;
    return true;
}
