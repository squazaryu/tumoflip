#pragma once

#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transmit a saved Sub-GHz file (.sub) on the Flipper radio.
 *
 * Adapted from the Quac! project's proven action_subghz_tx flow.
 *
 * @param storage      open Storage record
 * @param path         absolute path to the .sub file (e.g. /ext/subghz/foo.sub)
 * @param duration_ms  how long to keep keyed protocols transmitting
 * @param error        optional FuriString filled with a human-readable error
 * @return true on success
 */
bool companion_subghz_tx_file(
    Storage* storage,
    const char* path,
    uint32_t duration_ms,
    FuriString* error);

#ifdef __cplusplus
}
#endif
