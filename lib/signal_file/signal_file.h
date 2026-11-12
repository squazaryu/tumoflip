#pragma once

#include <storage/storage.h>
#include <toolbox/stream/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Replace an existing signal only after saving and verifying its old contents.
 * A pre-existing .tumo-bak is never overwritten. On a failed update a verified
 * backup is retained, even if rollback succeeds. This is recovery, not an atomic
 * power-loss transaction. Caller must serialize writes to this path.
 */
bool signal_file_update(Storage* storage, Stream* source, const char* path);

/** Replace from a closed file, allowing empty lists and create-new destinations.
 * The source is never removed. Existing destination backups are never clobbered.
 * Like signal_file_update, this requires caller-serialized access, not power-loss atomicity.
 */
bool signal_file_replace(Storage* storage, const char* source, const char* path);

#ifdef __cplusplus
}
#endif
