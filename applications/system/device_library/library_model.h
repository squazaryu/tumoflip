#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LIBRARY_LINKS 8U
#define LIBRARY_SLOTS 4U
typedef struct {
    char name[32];
    char notes[128];
    char tags[64];
    char links[LIBRARY_LINKS][256];
    uint32_t link_count;
} DeviceCard;
bool library_name_valid(const char* name);
bool library_path_valid(const char* path);
bool library_card_valid(const DeviceCard* card);
int library_newest_slot(const uint32_t generations[LIBRARY_SLOTS]);
unsigned library_write_slot(const uint32_t generations[LIBRARY_SLOTS]);
