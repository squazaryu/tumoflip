#pragma once

#include "desktop_panel_i.h"
#include <saved_struct.h>
#include <crc32_calc.h>

#define DESKTOP_PANEL_BANK0   "/int/.desktop_cards2.0"
#define DESKTOP_PANEL_BANK1   "/int/.desktop_cards2.1"
#define DESKTOP_PANEL_MAGIC   0xD7
#define DESKTOP_PANEL_VERSION 2

typedef struct {
    DesktopPanelSettings settings;
    uint32_t crc;
} DesktopPanelRecord;

static inline const char* desktop_panel_bank_path(uint8_t bank) {
    return bank ? DESKTOP_PANEL_BANK1 : DESKTOP_PANEL_BANK0;
}

static inline bool desktop_panel_read_bank(uint8_t bank, DesktopPanelRecord* record) {
    const char* path = desktop_panel_bank_path(bank);
    uint8_t magic, version;
    size_t size;
    return saved_struct_get_metadata(path, &magic, &version, &size) &&
           magic == DESKTOP_PANEL_MAGIC && version == DESKTOP_PANEL_VERSION &&
           size == sizeof(*record) &&
           saved_struct_load(path, record, sizeof(*record), magic, version) &&
           record->settings.generation && desktop_panel_valid(&record->settings) &&
           record->crc == crc32_calc_buffer(0, &record->settings, sizeof(record->settings));
}

/* Failed reads never replace the caller's last known-good RAM profile. */
static inline bool desktop_panel_latest(DesktopPanelSettings* panel, uint8_t* bank) {
    bool found = false;
    DesktopPanelRecord candidate;
    for(uint8_t i = 0; i < 2; ++i) {
        if(desktop_panel_read_bank(i, &candidate) &&
           (!found || (int32_t)(candidate.settings.generation - panel->generation) > 0)) {
            *panel = candidate.settings;
            if(bank) *bank = i;
            found = true;
        }
    }
    return found;
}

static inline bool desktop_panel_load(DesktopPanelSettings* panel) {
    return desktop_panel_latest(panel, NULL);
}

/* The previous bank is never erased. False means unconfirmed, not permission to
 * activate an unverified RAM profile; a complete new bank may still exist. */
static inline bool desktop_panel_store(DesktopPanelSettings* panel) {
    if(!desktop_panel_valid(panel)) return false;
    DesktopPanelSettings current;
    uint8_t bank = 1;
    if(desktop_panel_latest(&current, &bank)) {
        uint32_t generation = current.generation;
        current.generation = panel->generation;
        /* A read-back retry may discover the complete write it could not confirm. */
        if(!memcmp(&current, panel, sizeof(current))) {
            panel->generation = generation;
            return true;
        }
        if(generation != panel->generation) return false;
    } else if(panel->generation)
        return false;

    DesktopPanelRecord record = {.settings = *panel}, verified;
    record.settings.generation = panel->generation + 1;
    if(!record.settings.generation) record.settings.generation = 1;
    record.crc = crc32_calc_buffer(0, &record.settings, sizeof(record.settings));
    const uint8_t target = bank ^ 1U;
    if(!saved_struct_save(
           desktop_panel_bank_path(target),
           &record,
           sizeof(record),
           DESKTOP_PANEL_MAGIC,
           DESKTOP_PANEL_VERSION) ||
       !desktop_panel_read_bank(target, &verified) || memcmp(&record, &verified, sizeof(record)))
        return false;
    *panel = record.settings;
    return true;
}
