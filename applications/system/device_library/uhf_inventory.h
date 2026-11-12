#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UHF_INVENTORY_TAGS 32U
#define UHF_INVENTORY_EPC 48U

typedef struct {
    uint8_t epc[UHF_INVENTORY_EPC];
    uint8_t length;
    uint8_t antenna;
    uint16_t pc;
    uint32_t signal_raw;
    uint32_t frequency_raw;
    uint32_t seen;
    uint32_t first;
    uint32_t last;
} UhfInventoryTag;

typedef struct {
    uint8_t frame[257];
    uint16_t used;
    uint8_t version[3];
    uint8_t count;
    bool identified;
    uint32_t errors;
    uint32_t full;
    uint32_t completed;
    UhfInventoryTag tags[UHF_INVENTORY_TAGS];
} UhfInventory;

void uhf_inventory_init(UhfInventory* inventory);
void uhf_inventory_discontinuity(UhfInventory* inventory);
bool uhf_inventory_feed(UhfInventory* inventory, uint8_t byte, uint32_t timestamp);
size_t uhf_inventory_command(uint8_t command, uint8_t out[6]);
bool uhf_inventory_epc(const UhfInventoryTag* tag, char* output, size_t capacity);
