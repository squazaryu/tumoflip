#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LIBRARY_LINKS 8U
#define LIBRARY_SLOTS 4U
#define LIBRARY_PINS 16U
typedef struct {
    char name[32];
    char notes[128];
    char tags[64];
    char checked_at[24];
    char links[LIBRARY_LINKS][256];
    uint32_t link_count;
    char labels[LIBRARY_LINKS][32];
} DeviceCard;
typedef struct {
    uint32_t count;
    char names[LIBRARY_PINS][32];
} LibraryPins;
typedef enum {
    LibraryFileReference, LibraryFileSub, LibraryFileIr, LibraryFileNfc,
    LibraryFileRfid, LibraryFileIbutton,
} LibraryFileKind;
bool library_name_valid(const char* name);
bool library_path_valid(const char* path);
bool library_card_valid(const DeviceCard* card);
int library_newest_slot(const uint32_t generations[LIBRARY_SLOTS]);
unsigned library_write_slot(const uint32_t generations[LIBRARY_SLOTS]);
bool library_action_label(const DeviceCard* card, unsigned index, char* output, size_t size);
bool library_remove_link(DeviceCard* card, unsigned index);
LibraryFileKind library_file_kind(const char* path);
const char* library_launch_target(LibraryFileKind kind);
bool library_pins_valid(const LibraryPins* pins);
bool library_pins_contains(const LibraryPins* pins, const char* name);
bool library_pins_toggle(LibraryPins* pins, const char* name);
