#pragma once

#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Emulate a saved 125 kHz RFID file (.rfid). Adapted from Quac! action_rfid. */
bool companion_rfid_emulate(
    Storage* storage,
    const char* path,
    uint32_t duration_ms,
    FuriString* error);

/** Emulate a saved NFC file (.nfc). Adapted from Quac! action_nfc. */
bool companion_nfc_emulate(const char* path, uint32_t duration_ms, FuriString* error);

#ifdef __cplusplus
}
#endif
