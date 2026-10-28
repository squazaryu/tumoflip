#include "uhf_inventory.h"
#include <string.h>

static void uhf_increment(uint32_t* value) {
    if(*value != UINT32_MAX) ++*value;
}

void uhf_inventory_init(UhfInventory* inventory) {
    if(inventory) memset(inventory, 0, sizeof(*inventory));
}

void uhf_inventory_discontinuity(UhfInventory* inventory) {
    if(!inventory) return;
    inventory->used = 0;
    uhf_increment(&inventory->errors);
}

size_t uhf_inventory_command(uint8_t command, uint8_t out[6]) {
    if(!out || (command != 0x72U && command != 0x89U && command != 0x8CU)) return 0;
    const size_t length = command == 0x89U ? 6U : 5U;
    out[0] = 0xA0;
    out[1] = length - 2U;
    out[2] = 0;
    out[3] = command;
    if(length == 6U) out[4] = 1;
    uint8_t sum = 0;
    for(size_t i = 0; i < length - 1U; i++) sum += out[i];
    out[length - 1U] = (uint8_t)-sum;
    return length;
}

static uint32_t uhf_be(const uint8_t* data, unsigned size) {
    uint32_t value = 0;
    for(unsigned i = 0; i < size; i++) value = value * 256U + data[i];
    return value;
}

static bool uhf_inventory_tag(
    UhfInventory* inventory, const uint8_t* data, size_t length, uint8_t command,
    uint32_t timestamp) {
    if(length <= 7U) {
        uhf_increment(&inventory->completed);
        return false; // completion/error status is not an EPC
    }
    const bool buffered = command == 0x8AU;
    const size_t pc_offset = 1U;
    const uint16_t pc = (data[pc_offset] << 8U) | data[pc_offset + 1U];
    const size_t epc_size = ((pc >> 11U) & 31U) * 2U;
    const size_t meta = buffered ? 1U + data[0] : 3U + epc_size;
    if(!epc_size || epc_size > UHF_INVENTORY_EPC ||
       (buffered && data[0] != epc_size + 4U) ||
       length != meta + (buffered ? 9U : 7U)) return false;
    const uint8_t antenna = buffered ? data[meta + 7U] : data[0];
    if(antenna > 3U) return false;
    unsigned index = 0;
    while(index < inventory->count &&
          (inventory->tags[index].length != epc_size ||
           memcmp(inventory->tags[index].epc, data + 3, epc_size))) ++index;
    if(index == inventory->count) {
        if(inventory->count == UHF_INVENTORY_TAGS) {
            uhf_increment(&inventory->full);
            return false; // never silently evict an observed identity
        }
        inventory->count++;
        inventory->tags[index].first = timestamp;
    }
    UhfInventoryTag* tag = &inventory->tags[index];
    memcpy(tag->epc, data + 3, epc_size);
    tag->length = epc_size;
    tag->pc = pc;
    tag->antenna = antenna;
    tag->signal_raw = uhf_be(data + meta, 4);
    tag->frequency_raw = uhf_be(data + meta + 4U, 3);
    tag->last = timestamp;
    const uint32_t hits = buffered && data[meta + 8U] ? data[meta + 8U] : 1U;
    tag->seen = hits > UINT32_MAX - tag->seen ? UINT32_MAX : tag->seen + hits;
    return true;
}

static void uhf_inventory_shift(UhfInventory* inventory) {
    unsigned offset = 1;
    while(offset < inventory->used && inventory->frame[offset] != 0xA0U) ++offset;
    inventory->used -= offset;
    memmove(inventory->frame, inventory->frame + offset, inventory->used);
}

bool uhf_inventory_feed(UhfInventory* inventory, uint8_t byte, uint32_t timestamp) {
    if(!inventory) return false;
    if(!inventory->used && byte != 0xA0U) return false;
    if(inventory->used == sizeof(inventory->frame)) uhf_inventory_discontinuity(inventory);
    inventory->frame[inventory->used++] = byte;
    bool changed = false;
    while(inventory->used >= 2U) {
        const size_t length = inventory->frame[1] + 2U;
        if(length < 5U) {
            uhf_increment(&inventory->errors);
            uhf_inventory_shift(inventory);
            continue;
        }
        if(inventory->used < length) break;
        uint8_t sum = 0;
        for(size_t i = 0; i < length; i++) sum += inventory->frame[i];
        if(sum || inventory->frame[2] != 0U) {
            uhf_increment(&inventory->errors);
            uhf_inventory_shift(inventory);
            continue;
        }
        const uint8_t command = inventory->frame[3];
        const uint8_t* data = inventory->frame + 4U;
        if(command == 0x72U && length == 8U) {
            memcpy(inventory->version, data, sizeof(inventory->version));
            inventory->identified = true;
            changed = true;
        } else if(command == 0x89U || command == 0x8AU) {
            changed = uhf_inventory_tag(inventory, data, length - 5U, command, timestamp) || changed;
        }
        inventory->used -= length;
        memmove(inventory->frame, inventory->frame + length, inventory->used);
        while(inventory->used && inventory->frame[0] != 0xA0U) uhf_inventory_shift(inventory);
    }
    return changed;
}

bool uhf_inventory_epc(const UhfInventoryTag* tag, char* output, size_t capacity) {
    if(!tag || !output || !tag->length || tag->length > UHF_INVENTORY_EPC ||
       capacity <= 2U * tag->length) return false;
    static const char hex[] = "0123456789ABCDEF";
    for(unsigned i = 0; i < tag->length; i++) {
        output[i * 2U] = hex[tag->epc[i] >> 4U];
        output[i * 2U + 1U] = hex[tag->epc[i] & 15U];
    }
    output[2U * tag->length] = 0;
    return true;
}
